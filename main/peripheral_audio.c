#include "peripheral_audio.h"
#include "config.h"
#include "gpio_policy.h"

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "periph_audio";

static i2s_chan_handle_t s_mic_chan = NULL;
static i2s_chan_handle_t s_speaker_chan = NULL;

static bool s_mic_running = false;
static bool s_speaker_running = false;

// The level of the last captured buffer. It is the loudest sample, which
// is enough to show that the microphone picks up a sound.
static volatile int s_mic_peak = 0;

// The buffer the microphone reads into. It is allocated once and reused.
static int16_t *s_mic_buffer = NULL;

// The buffer the speaker plays out of.
static int16_t *s_speaker_buffer = NULL;

static i2s_chan_handle_t create_channel(i2s_chan_handle_t *out_handle,
                                        int bclk_pin,
                                        int ws_pin,
                                        int data_pin,
                                        i2s_dir_t direction)
{
    i2s_chan_config_t chan_config = I2S_CHANNEL_DEFAULT_CONFIG(direction, I2S_NUM_0);
    i2s_chan_handle_t handle = NULL;

    esp_err_t err = i2s_new_channel(&chan_config, out_handle, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create the I2S channel");
        return NULL;
    }

    i2s_std_config_t std_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(PERIPHERAL_AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(PERIPHERAL_AUDIO_SAMPLE_BITS,
                                                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = bclk_pin,
            .ws = ws_pin,
            .dout = direction == I2S_DIR_TX ? data_pin : I2S_GPIO_UNUSED,
            .din = direction == I2S_DIR_RX ? data_pin : I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(handle, &std_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize the I2S channel in standard mode");
        i2s_del_channel(handle);
        return NULL;
    }

    return handle;
}

esp_err_t peripheral_mic_start(const peripheral_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!gpio_policy_pin_is_allowed(config->pin_din) ||
        !gpio_policy_pin_is_allowed(config->pin_b)) {
        return ESP_ERR_INVALID_ARG;
    }

    peripheral_mic_stop();

    i2s_chan_handle_t handle = create_channel(&s_mic_chan,
                                              config->pin_b,   // BCLK
                                              config->pin_ws,  // WS
                                              config->pin_din, // data in
                                              I2S_DIR_RX);
    if (!handle) {
        return ESP_FAIL;
    }

    if (!s_mic_buffer) {
        s_mic_buffer = calloc(1, PERIPHERAL_AUDIO_BUFFER_BYTES);
        if (!s_mic_buffer) {
            ESP_LOGE(TAG, "Failed to allocate the microphone buffer");
            i2s_del_channel(s_mic_chan);
            s_mic_chan = NULL;
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t err = i2s_channel_enable(s_mic_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable the microphone channel");
        i2s_del_channel(s_mic_chan);
        s_mic_chan = NULL;
        return err;
    }

    s_mic_running = true;
    ESP_LOGI(TAG, "Microphone is ready on BCLK=%d WS=%d DIN=%d",
             config->pin_b, config->pin_ws, config->pin_din);
    return ESP_OK;
}

void peripheral_mic_stop(void)
{
    if (!s_mic_running) {
        return;
    }

    if (s_mic_chan) {
        i2s_channel_disable(s_mic_chan);
        i2s_del_channel(s_mic_chan);
        s_mic_chan = NULL;
    }

    s_mic_running = false;
    ESP_LOGI(TAG, "Microphone stopped");
}

bool peripheral_mic_is_running(void)
{
    return s_mic_running;
}

esp_err_t peripheral_mic_read_level(int *level_out)
{
    if (!s_mic_running || !s_mic_buffer) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t bytes_read = 0;
    esp_err_t err = i2s_channel_read(s_mic_chan, s_mic_buffer,
                                     PERIPHERAL_AUDIO_BUFFER_BYTES,
                                     &bytes_read, pdMS_TO_TICKS(200));
    if (err != ESP_OK) {
        return err;
    }

    int samples = (int)(bytes_read / sizeof(int16_t));
    int peak = 0;
    for (int i = 0; i < samples; ++i) {
        int value = s_mic_buffer[i];
        if (value < 0) {
            value = -value;
        }
        if (value > peak) {
            peak = value;
        }
    }

    s_mic_peak = peak;
    if (level_out) {
        *level_out = peak;
    }
    return ESP_OK;
}

int peripheral_mic_peak(void)
{
    return s_mic_peak;
}

esp_err_t peripheral_speaker_start(const peripheral_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!gpio_policy_pin_is_allowed(config->pin_dout) ||
        !gpio_policy_pin_is_allowed(config->pin_b)) {
        return ESP_ERR_INVALID_ARG;
    }

    peripheral_speaker_stop();

    i2s_chan_handle_t handle = create_channel(&s_speaker_chan,
                                              config->pin_b,    // BCLK
                                              config->pin_ws,   // WS
                                              config->pin_dout, // data out
                                              I2S_DIR_TX);
    if (!handle) {
        return ESP_FAIL;
    }

    if (!s_speaker_buffer) {
        s_speaker_buffer = calloc(1, PERIPHERAL_AUDIO_BUFFER_BYTES);
        if (!s_speaker_buffer) {
            ESP_LOGE(TAG, "Failed to allocate the speaker buffer");
            i2s_del_channel(s_speaker_chan);
            s_speaker_chan = NULL;
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t err = i2s_channel_enable(s_speaker_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable the speaker channel");
        i2s_del_channel(s_speaker_chan);
        s_speaker_chan = NULL;
        return err;
    }

    s_speaker_running = true;
    ESP_LOGI(TAG, "Speaker is ready on BCLK=%d WS=%d DOUT=%d",
             config->pin_b, config->pin_ws, config->pin_dout);
    return ESP_OK;
}

void peripheral_speaker_stop(void)
{
    if (!s_speaker_running) {
        return;
    }

    if (s_speaker_chan) {
        i2s_channel_disable(s_speaker_chan);
        i2s_del_channel(s_speaker_chan);
        s_speaker_chan = NULL;
    }

    s_speaker_running = false;
    ESP_LOGI(TAG, "Speaker stopped");
}

bool peripheral_speaker_is_running(void)
{
    return s_speaker_running;
}

esp_err_t peripheral_speaker_play_tone(int frequency_hz, int duration_ms)
{
    if (!s_speaker_running || !s_speaker_buffer) {
        return ESP_ERR_INVALID_STATE;
    }
    if (frequency_hz <= 0 || duration_ms <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // The tone is written in blocks so a long tone does not need a big
    // buffer. The amplitude is kept low to protect the speaker.
    int samples = (PERIPHERAL_AUDIO_SAMPLE_RATE * duration_ms) / 1000;
    int written = 0;
    const int amplitude = 8000;

    while (written < samples) {
        int chunk = samples - written;
        if (chunk > PERIPHERAL_AUDIO_BUFFER_SAMPLES) {
            chunk = PERIPHERAL_AUDIO_BUFFER_SAMPLES;
        }

        for (int i = 0; i < chunk; ++i) {
            double phase = 2.0 * M_PI * frequency_hz * (written + i) /
                           PERIPHERAL_AUDIO_SAMPLE_RATE;
            s_speaker_buffer[i] = (int16_t)(amplitude * sin(phase));
        }

        size_t bytes_written = 0;
        esp_err_t err = i2s_channel_write(s_speaker_chan,
                                          s_speaker_buffer,
                                          (size_t)chunk * sizeof(int16_t),
                                          &bytes_written,
                                          pdMS_TO_TICKS(1000));
        if (err != ESP_OK) {
            return err;
        }

        written += chunk;
    }

    return ESP_OK;
}

esp_err_t peripheral_speaker_beep(void)
{
    return peripheral_speaker_play_tone(1000, 120);
}
