/*
 * Mock of the peripheral layer for host testing.
 */

#include "mock_peripheral.h"

#include <string.h>

static int s_set_count = 0;
static peripheral_type_t s_last_type = PERIPHERAL_NONE;
static peripheral_config_t s_last_config;
static bool s_running[PERIPHERAL_TYPE_COUNT] = {false};
static peripheral_input_msg_t s_input_events[8];
static int s_input_count = 0;

// The results the output drivers hand back.
static int s_analog_raw = 0;
static int s_analog_millivolts = 0;
static int s_mic_level = 0;
static int s_speaker_frequency = 0;
static int s_speaker_duration = 0;
static int s_display_rows = 8;
static char s_display_text[64];

void mock_peripheral_reset(void)
{
    s_set_count = 0;
    s_last_type = PERIPHERAL_NONE;
    memset(&s_last_config, 0, sizeof(s_last_config));
    memset(s_running, 0, sizeof(s_running));
    s_input_count = 0;
    s_analog_raw = 0;
    s_analog_millivolts = 0;
    s_mic_level = 0;
    s_speaker_frequency = 0;
    s_speaker_duration = 0;
    s_display_rows = 8;
    s_display_text[0] = '\0';
}

void mock_peripheral_set_analog(int raw, int millivolts)
{
    s_analog_raw = raw;
    s_analog_millivolts = millivolts;
}

int mock_peripheral_get_analog_raw(void)
{
    return s_analog_raw;
}

int mock_peripheral_get_analog_millivolts(void)
{
    return s_analog_millivolts;
}

void mock_peripheral_set_mic_level(int level)
{
    s_mic_level = level;
}

int mock_peripheral_get_mic_level(void)
{
    return s_mic_level;
}

void mock_peripheral_set_speaker_tone(int frequency, int duration)
{
    s_speaker_frequency = frequency;
    s_speaker_duration = duration;
}

int mock_peripheral_get_speaker_frequency(void)
{
    return s_speaker_frequency;
}

int mock_peripheral_get_speaker_duration(void)
{
    return s_speaker_duration;
}

void mock_peripheral_set_display_text(const char *text)
{
    snprintf(s_display_text, sizeof(s_display_text), "%s", text ? text : "");
}

const char *mock_peripheral_get_display_text(void)
{
    return s_display_text;
}

int mock_peripheral_get_display_rows(void)
{
    return s_display_rows;
}

int mock_peripheral_set_count(void)
{
    return s_set_count;
}

peripheral_type_t mock_peripheral_last_type(void)
{
    return s_last_type;
}

const peripheral_config_t *mock_peripheral_last_config(void)
{
    return &s_last_config;
}

void mock_peripheral_push_input(peripheral_input_msg_t msg)
{
    if (s_input_count < (int)(sizeof(s_input_events) / sizeof(s_input_events[0]))) {
        s_input_events[s_input_count++] = msg;
    }
}

// -----------------------------------------------------------------------------
// The peripheral API
// -----------------------------------------------------------------------------

const char *peripheral_type_name(peripheral_type_t type)
{
    switch (type) {
        case PERIPHERAL_BUTTON:
            return "button";
        case PERIPHERAL_ENCODER:
            return "encoder";
        case PERIPHERAL_ANALOG:
            return "analog";
        case PERIPHERAL_DISPLAY_OLED:
            return "oled";
        case PERIPHERAL_DISPLAY_LCD:
            return "lcd";
        case PERIPHERAL_MIC:
            return "mic";
        case PERIPHERAL_SPEAKER:
            return "speaker";
        default:
            return "none";
    }
}

bool peripheral_type_from_name(const char *name, peripheral_type_t *type_out)
{
    if (!name || !type_out) {
        return false;
    }

    for (int i = 1; i < PERIPHERAL_TYPE_COUNT; ++i) {
        if (strcmp(name, peripheral_type_name((peripheral_type_t)i)) == 0) {
            *type_out = (peripheral_type_t)i;
            return true;
        }
    }

    // The aliases the agent uses for the words a user would say.
    if (strcmp(name, "switch") == 0) {
        *type_out = PERIPHERAL_BUTTON;
        return true;
    }
    if (strcmp(name, "knob") == 0 || strcmp(name, "rotary") == 0) {
        *type_out = PERIPHERAL_ENCODER;
        return true;
    }
    if (strcmp(name, "sensor") == 0) {
        *type_out = PERIPHERAL_ANALOG;
        return true;
    }
    if (strcmp(name, "display") == 0 || strcmp(name, "screen") == 0) {
        *type_out = PERIPHERAL_DISPLAY_OLED;
        return true;
    }
    if (strcmp(name, "buzzer") == 0) {
        *type_out = PERIPHERAL_SPEAKER;
        return true;
    }
    if (strcmp(name, "microphone") == 0) {
        *type_out = PERIPHERAL_MIC;
        return true;
    }

    return false;
}

bool peripheral_get_config(peripheral_type_t type, peripheral_config_t *config_out)
{
    if (type <= PERIPHERAL_NONE || type >= PERIPHERAL_TYPE_COUNT) {
        return false;
    }
    if (!config_out || s_last_type != type) {
        return false;
    }

    *config_out = s_last_config;
    return true;
}

esp_err_t peripheral_set_config(peripheral_type_t type, const peripheral_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    s_set_count++;
    s_last_type = type;
    s_last_config = *config;
    return ESP_OK;
}

esp_err_t peripheral_remove_config(peripheral_type_t type)
{
    if (type <= PERIPHERAL_NONE || type >= PERIPHERAL_TYPE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_last_type == type) {
        s_last_type = PERIPHERAL_NONE;
        memset(&s_last_config, 0, sizeof(s_last_config));
    }
    return ESP_OK;
}

esp_err_t peripheral_start(peripheral_type_t type)
{
    if (type <= PERIPHERAL_NONE || type >= PERIPHERAL_TYPE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    s_running[type] = true;
    return ESP_OK;
}

void peripheral_stop(peripheral_type_t type)
{
    if (type <= PERIPHERAL_NONE || type >= PERIPHERAL_TYPE_COUNT) {
        return;
    }

    s_running[type] = false;
}

bool peripheral_is_running(peripheral_type_t type)
{
    if (type <= PERIPHERAL_NONE || type >= PERIPHERAL_TYPE_COUNT) {
        return false;
    }
    return s_running[type];
}

void peripheral_start_all(void)
{
}

void peripheral_stop_all(void)
{
}

int peripheral_configured_count(void)
{
    return s_last_type == PERIPHERAL_NONE ? 0 : 1;
}

void peripheral_report(char *buf, size_t buf_len)
{
    if (!buf || buf_len == 0) {
        return;
    }

    snprintf(buf, buf_len, "Configured peripherals: %s",
             s_last_type == PERIPHERAL_NONE ? "none"
                                            : peripheral_type_name(s_last_type));
}

// -----------------------------------------------------------------------------
// The input API
// -----------------------------------------------------------------------------

bool peripheral_input_pop(peripheral_input_msg_t *msg_out)
{
    if (!msg_out || s_input_count == 0) {
        return false;
    }

    *msg_out = s_input_events[0];
    memmove(&s_input_events[0], &s_input_events[1],
            (size_t)(s_input_count - 1) * sizeof(s_input_events[0]));
    s_input_count--;
    return true;
}

int peripheral_input_pending(void)
{
    return s_input_count;
}

const char *peripheral_input_event_name(peripheral_input_event_t event)
{
    switch (event) {
        case PERIPHERAL_INPUT_EVENT_BUTTON_DOWN:
            return "button down";
        case PERIPHERAL_INPUT_EVENT_BUTTON_UP:
            return "button up";
        case PERIPHERAL_INPUT_EVENT_BUTTON_LONG_PRESS:
            return "button long press";
        case PERIPHERAL_INPUT_EVENT_ENCODER_CW:
            return "encoder clockwise";
        case PERIPHERAL_INPUT_EVENT_ENCODER_CCW:
            return "encoder counter-clockwise";
        case PERIPHERAL_INPUT_EVENT_ENCODER_BUTTON_DOWN:
            return "encoder button down";
        case PERIPHERAL_INPUT_EVENT_ENCODER_BUTTON_UP:
            return "encoder button up";
        default:
            return "none";
    }
}

// -----------------------------------------------------------------------------
// The output API
// -----------------------------------------------------------------------------

esp_err_t peripheral_analog_read(const peripheral_config_t *config,
                                 int *raw_out,
                                 int *millivolts_out)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    if (raw_out) {
        *raw_out = s_analog_raw;
    }
    if (millivolts_out) {
        *millivolts_out = s_analog_millivolts;
    }
    return ESP_OK;
}

esp_err_t peripheral_display_write_line(int row, const char *text)
{
    (void)row;
    snprintf(s_display_text, sizeof(s_display_text), "%s", text ? text : "");
    return ESP_OK;
}

esp_err_t peripheral_display_clear(void)
{
    s_display_text[0] = '\0';
    return ESP_OK;
}

int peripheral_display_rows(void)
{
    return s_display_rows;
}

esp_err_t peripheral_speaker_play_tone(int frequency_hz, int duration_ms)
{
    s_speaker_frequency = frequency_hz;
    s_speaker_duration = duration_ms;
    return ESP_OK;
}

esp_err_t peripheral_mic_read_level(int *level_out)
{
    if (level_out) {
        *level_out = s_mic_level;
    }
    return ESP_OK;
}
