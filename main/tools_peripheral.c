#include "tools_handlers.h"
#include "tools_common.h"
#include "config.h"
#include "gpio_policy.h"
#include "peripheral.h"
#include "peripheral_audio.h"
#include "peripheral_display.h"
#include "peripheral_input.h"
#include "peripheral_output.h"

#include "esp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The tools that let the assistant set up the hardware.
//
// The user describes the wiring in plain language, the model turns it into
// these calls, and the firmware stores the result. The next boot brings the
// drivers up again, so the setup survives a restart.

// Read one optional pin number from the input.
static int read_pin(const cJSON *input, const char *field, int fallback)
{
    cJSON *item = cJSON_GetObjectItem(input, field);

    if (!item || !cJSON_IsNumber(item)) {
        return fallback;
    }
    return item->valueint;
}

static bool read_bool(const cJSON *input, const char *field, bool fallback)
{
    cJSON *item = cJSON_GetObjectItem(input, field);

    if (!item || !cJSON_IsBool(item)) {
        return fallback;
    }
    return cJSON_IsTrue(item);
}

// The description of the pins of one peripheral type, so the model knows
// which field belongs to which pin.
static const char *pin_help(peripheral_type_t type)
{
    switch (type) {
        case PERIPHERAL_BUTTON:
            return "pin = the button pin";
        case PERIPHERAL_ENCODER:
            return "pin_a = encoder A, pin_b = encoder B, pin_sw = the push switch";
        case PERIPHERAL_ANALOG:
            return "pin = the analog input pin";
        case PERIPHERAL_DISPLAY_OLED:
            return "pin_sda = I2C data, pin_scl = I2C clock, address = 0x3C or 0x3D, "
                   "width and height in pixels";
        case PERIPHERAL_DISPLAY_LCD:
            return "pin_mosi = data in, pin_clk = clock, pin_dc = data/command, "
                   "pin_cs = chip select, pin_rst = reset, width and height in pixels";
        case PERIPHERAL_MIC:
            return "pin_b = BCLK, pin_ws = word select, pin_din = data in";
        case PERIPHERAL_SPEAKER:
            return "pin_b = BCLK, pin_ws = word select, pin_dout = data out";
        default:
            return "no pins";
    }
}

static void fill_defaults(peripheral_config_t *config, peripheral_type_t type)
{
    memset(config, 0, sizeof(*config));

    config->pin_a = -1;
    config->pin_b = -1;
    config->pin_sw = -1;
    config->pin_sda = -1;
    config->pin_scl = -1;
    config->pin_miso = -1;
    config->pin_mosi = -1;
    config->pin_dc = -1;
    config->pin_cs = -1;
    config->pin_clk = -1;
    config->pin_rst = -1;
    config->pin_ws = -1;
    config->pin_din = -1;
    config->pin_dout = -1;
    config->pin_amp_bclk = -1;
    config->pin_amp_lr = -1;
    config->pin_amp_din = -1;

    config->pullup = true;

    switch (type) {
        case PERIPHERAL_DISPLAY_OLED:
            config->address = 0x3C;
            config->width = 128;
            config->height = 64;
            break;
        case PERIPHERAL_DISPLAY_LCD:
            config->width = 240;
            config->height = 240;
            config->frequency_hz = 40000000;
            break;
        default:
            break;
    }
}

bool tools_peripheral_setup_handler(const cJSON *input, char *result, size_t result_len)
{
    cJSON *type_json = cJSON_GetObjectItem(input, "type");
    peripheral_type_t type = PERIPHERAL_NONE;

    if (!type_json || !cJSON_IsString(type_json)) {
        snprintf(result, result_len,
                 "Error: 'type' required (button, encoder, analog, oled, lcd, mic or speaker)");
        return false;
    }

    if (!peripheral_type_from_name(type_json->valuestring, &type)) {
        snprintf(result, result_len,
                 "Error: unknown peripheral type '%s' "
                 "(use button, encoder, analog, oled, lcd, mic or speaker)",
                 type_json->valuestring);
        return false;
    }

    peripheral_config_t config;
    fill_defaults(&config, type);

    // The stored configuration is the base, so a partial update keeps the
    // pins that were not mentioned.
    peripheral_config_t stored;
    if (peripheral_get_config(type, &stored)) {
        config = stored;
    }

    config.pin_a = read_pin(input, "pin_a", read_pin(input, "pin", config.pin_a));
    config.pin_b = read_pin(input, "pin_b", config.pin_b);
    config.pin_sw = read_pin(input, "pin_sw", config.pin_sw);
    config.pin_sda = read_pin(input, "pin_sda", config.pin_sda);
    config.pin_scl = read_pin(input, "pin_scl", config.pin_scl);
    config.pin_miso = read_pin(input, "pin_miso", config.pin_miso);
    config.pin_mosi = read_pin(input, "pin_mosi", config.pin_mosi);
    config.pin_dc = read_pin(input, "pin_dc", config.pin_dc);
    config.pin_cs = read_pin(input, "pin_cs", config.pin_cs);
    config.pin_clk = read_pin(input, "pin_clk", config.pin_clk);
    config.pin_rst = read_pin(input, "pin_rst", config.pin_rst);
    config.pin_ws = read_pin(input, "pin_ws", config.pin_ws);
    config.pin_din = read_pin(input, "pin_din", config.pin_din);
    config.pin_dout = read_pin(input, "pin_dout", config.pin_dout);
    config.address = read_pin(input, "address", config.address);
    config.frequency_hz = read_pin(input, "frequency_hz", config.frequency_hz);
    config.width = read_pin(input, "width", config.width);
    config.height = read_pin(input, "height", config.height);
    config.pullup = read_bool(input, "pullup", config.pullup);
    config.inverted = read_bool(input, "inverted", config.inverted);

    // The pins have to pass the safety policy, otherwise the agent could
    // take over the flash pins of the chip.
    int pins[14];
    int pin_count = 0;
    pins[pin_count++] = config.pin_a;
    pins[pin_count++] = config.pin_b;
    pins[pin_count++] = config.pin_sw;
    pins[pin_count++] = config.pin_sda;
    pins[pin_count++] = config.pin_scl;
    pins[pin_count++] = config.pin_mosi;
    pins[pin_count++] = config.pin_clk;
    pins[pin_count++] = config.pin_dc;
    pins[pin_count++] = config.pin_cs;
    pins[pin_count++] = config.pin_rst;
    pins[pin_count++] = config.pin_ws;
    pins[pin_count++] = config.pin_din;
    pins[pin_count++] = config.pin_dout;

    for (int i = 0; i < pin_count; ++i) {
        if (pins[i] < 0) {
            continue;
        }
        if (!gpio_policy_pin_is_allowed(pins[i])) {
            char hint[128];
            if (gpio_policy_pin_forbidden_hint(pins[i], hint, sizeof(hint))) {
                snprintf(result, result_len, "%s", hint);
            } else if (GPIO_ALLOWED_PINS_CSV[0] != '\0') {
                snprintf(result, result_len,
                         "Error: pin %d is not in the allowlist of this device", pins[i]);
            } else {
                snprintf(result, result_len,
                         "Error: pin %d is not allowed, the allowed range is %d-%d",
                         pins[i], GPIO_MIN_PIN, GPIO_MAX_PIN);
            }
            return false;
        }
    }

    esp_err_t err = peripheral_set_config(type, &config);
    if (err != ESP_OK) {
        snprintf(result, result_len, "Error: failed to store the configuration (%s)",
                 esp_err_to_name(err));
        return false;
    }

    // Bring the driver up right away so the user sees the result at once.
    if (type != PERIPHERAL_ANALOG) {
        err = peripheral_start(type);
        if (err != ESP_OK) {
            snprintf(result, result_len,
                     "The %s was stored but the driver failed to start (%s). "
                     "Check the wiring and the %s.",
                     peripheral_type_name(type), esp_err_to_name(err),
                     pin_help(type));
            return false;
        }
    }

    snprintf(result, result_len,
             "The %s is set up and running. Pin assignment: %s",
             peripheral_type_name(type), pin_help(type));
    return true;
}

bool tools_peripheral_list_handler(const cJSON *input, char *result, size_t result_len)
{
    (void)input;

    peripheral_report(result, result_len);
    return true;
}

bool tools_peripheral_remove_handler(const cJSON *input, char *result, size_t result_len)
{
    cJSON *type_json = cJSON_GetObjectItem(input, "type");
    peripheral_type_t type = PERIPHERAL_NONE;

    if (!type_json || !cJSON_IsString(type_json)) {
        snprintf(result, result_len, "Error: 'type' required");
        return false;
    }

    if (!peripheral_type_from_name(type_json->valuestring, &type)) {
        snprintf(result, result_len, "Error: unknown peripheral type '%s'",
                 type_json->valuestring);
        return false;
    }

    peripheral_stop(type);

    // The entry disappears from the stored document, so the report and the
    // next boot skip this peripheral.
    esp_err_t err = peripheral_remove_config(type);
    if (err != ESP_OK) {
        snprintf(result, result_len, "Error: failed to remove the configuration (%s)",
                 esp_err_to_name(err));
        return false;
    }

    snprintf(result, result_len, "The %s was removed", peripheral_type_name(type));
    return true;
}

bool tools_peripheral_input_handler(const cJSON *input, char *result, size_t result_len)
{
    (void)input;

    int pending = peripheral_input_pending();
    if (pending == 0) {
        snprintf(result, result_len, "No input event since the last check");
        return true;
    }

    size_t offset = 0;
    int written = snprintf(result, result_len, "%d input event(s):", pending);
    if (written < 0) {
        result[0] = '\0';
        return false;
    }
    offset = (size_t)written < result_len ? (size_t)written : result_len - 1;

    peripheral_input_msg_t msg;
    while (peripheral_input_pop(&msg) && offset < result_len) {
        written = snprintf(result + offset, result_len - offset, " %s",
                           peripheral_input_event_name(msg.event));
        if (written < 0 || (size_t)written >= result_len - offset) {
            break;
        }
        offset += (size_t)written;
    }

    return true;
}

bool tools_peripheral_display_handler(const cJSON *input, char *result, size_t result_len)
{
    cJSON *text_json = cJSON_GetObjectItem(input, "text");
    cJSON *row_json = cJSON_GetObjectItem(input, "row");
    cJSON *clear_json = cJSON_GetObjectItem(input, "clear");

    bool running = peripheral_is_running(PERIPHERAL_DISPLAY_OLED) ||
                   peripheral_is_running(PERIPHERAL_DISPLAY_LCD);
    if (!running) {
        snprintf(result, result_len,
                 "Error: no display is set up yet. Set one up with peripheral_setup first.");
        return false;
    }

    if (clear_json && cJSON_IsTrue(clear_json)) {
        esp_err_t err = peripheral_display_clear();
        if (err != ESP_OK) {
            snprintf(result, result_len, "Error: failed to clear the display (%s)",
                     esp_err_to_name(err));
            return false;
        }
        snprintf(result, result_len, "Display cleared");
        return true;
    }

    if (!text_json || !cJSON_IsString(text_json)) {
        snprintf(result, result_len, "Error: 'text' required, or clear set to true");
        return false;
    }

    int row = row_json && cJSON_IsNumber(row_json) ? row_json->valueint : 0;
    esp_err_t err = peripheral_display_write_line(row, text_json->valuestring);
    if (err != ESP_OK) {
        snprintf(result, result_len, "Error: failed to write to the display (%s)",
                 esp_err_to_name(err));
        return false;
    }

    snprintf(result, result_len, "Wrote '%s' on row %d", text_json->valuestring, row);
    return true;
}

bool tools_peripheral_analog_handler(const cJSON *input, char *result, size_t result_len)
{
    (void)input;

    peripheral_config_t config;
    if (!peripheral_get_config(PERIPHERAL_ANALOG, &config)) {
        snprintf(result, result_len,
                 "Error: no analog sensor is set up yet. "
                 "Set one up with peripheral_setup first.");
        return false;
    }

    int raw = 0;
    int millivolts = 0;
    esp_err_t err = peripheral_analog_read(&config, &raw, &millivolts);
    if (err != ESP_OK) {
        snprintf(result, result_len, "Error: failed to read the sensor (%s)",
                 esp_err_to_name(err));
        return false;
    }

    snprintf(result, result_len,
             "Sensor on GPIO %d: raw=%d, %d mV",
             config.pin_a, raw, millivolts);
    return true;
}

bool tools_peripheral_speaker_handler(const cJSON *input, char *result, size_t result_len)
{
    cJSON *frequency_json = cJSON_GetObjectItem(input, "frequency_hz");
    cJSON *duration_json = cJSON_GetObjectItem(input, "duration_ms");

    if (!peripheral_is_running(PERIPHERAL_SPEAKER)) {
        snprintf(result, result_len,
                 "Error: no speaker is set up yet. "
                 "Set one up with peripheral_setup first.");
        return false;
    }

    int frequency = frequency_json && cJSON_IsNumber(frequency_json)
        ? frequency_json->valueint : 1000;
    int duration = duration_json && cJSON_IsNumber(duration_json)
        ? duration_json->valueint : 200;

    esp_err_t err = peripheral_speaker_play_tone(frequency, duration);
    if (err != ESP_OK) {
        snprintf(result, result_len, "Error: failed to play the tone (%s)",
                 esp_err_to_name(err));
        return false;
    }

    snprintf(result, result_len, "Played a %d Hz tone for %d ms", frequency, duration);
    return true;
}

bool tools_peripheral_mic_handler(const cJSON *input, char *result, size_t result_len)
{
    (void)input;

    if (!peripheral_is_running(PERIPHERAL_MIC)) {
        snprintf(result, result_len,
                 "Error: no microphone is set up yet. "
                 "Set one up with peripheral_setup first.");
        return false;
    }

    int level = 0;
    esp_err_t err = peripheral_mic_read_level(&level);
    if (err != ESP_OK) {
        snprintf(result, result_len, "Error: failed to read the microphone (%s)",
                 esp_err_to_name(err));
        return false;
    }

    snprintf(result, result_len, "Microphone level: %d of 32767%s", level,
             level > 2000 ? " (something is being picked up)" : " (quiet)");
    return true;
}
