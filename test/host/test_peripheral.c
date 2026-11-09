/*
 * Host tests for the peripheral layer.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "mock_peripheral.h"
#include "peripheral.h"
#include "peripheral_font.h"
#include "peripheral_input.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

TEST(type_names_round_trip)
{
    for (int i = 1; i < PERIPHERAL_TYPE_COUNT; ++i) {
        peripheral_type_t type = (peripheral_type_t)i;
        peripheral_type_t parsed = PERIPHERAL_NONE;

        ASSERT(peripheral_type_from_name(peripheral_type_name(type), &parsed));
        ASSERT(parsed == type);
    }

    return 0;
}

TEST(unknown_type_is_rejected)
{
    peripheral_type_t type = PERIPHERAL_BUTTON;

    ASSERT(!peripheral_type_from_name("floppy_disk", &type));
    ASSERT(!peripheral_type_from_name("", &type));
    ASSERT(!peripheral_type_from_name(NULL, &type));

    return 0;
}

TEST(aliases_are_accepted)
{
    peripheral_type_t type = PERIPHERAL_NONE;

    ASSERT(peripheral_type_from_name("switch", &type));
    ASSERT(type == PERIPHERAL_BUTTON);

    ASSERT(peripheral_type_from_name("knob", &type));
    ASSERT(type == PERIPHERAL_ENCODER);

    ASSERT(peripheral_type_from_name("sensor", &type));
    ASSERT(type == PERIPHERAL_ANALOG);

    ASSERT(peripheral_type_from_name("screen", &type));
    ASSERT(type == PERIPHERAL_DISPLAY_OLED);

    return 0;
}

TEST(config_round_trip)
{
    mock_peripheral_reset();

    peripheral_config_t config;
    memset(&config, 0, sizeof(config));
    config.pin_a = 5;
    config.pin_b = 6;
    config.pin_sw = 7;
    config.pin_sda = 4;
    config.pin_scl = 8;
    config.address = 0x3C;
    config.width = 128;
    config.height = 64;
    config.pullup = true;

    ASSERT(peripheral_set_config(PERIPHERAL_DISPLAY_OLED, &config) == ESP_OK);
    ASSERT(mock_peripheral_set_count() == 1);
    ASSERT(mock_peripheral_last_type() == PERIPHERAL_DISPLAY_OLED);

    peripheral_config_t stored;
    ASSERT(peripheral_get_config(PERIPHERAL_DISPLAY_OLED, &stored));
    ASSERT(stored.pin_sda == 4);
    ASSERT(stored.pin_scl == 8);
    ASSERT(stored.address == 0x3C);
    ASSERT(stored.width == 128);
    ASSERT(stored.height == 64);
    ASSERT(stored.pin_a == 5);
    ASSERT(stored.pin_b == 6);

    return 0;
}

TEST(unconfigured_type_has_no_config)
{
    mock_peripheral_reset();

    peripheral_config_t config;
    ASSERT(!peripheral_get_config(PERIPHERAL_ENCODER, &config));
    ASSERT(!peripheral_get_config(PERIPHERAL_NONE, &config));

    return 0;
}

TEST(start_and_stop_are_tracked)
{
    mock_peripheral_reset();

    ASSERT(!peripheral_is_running(PERIPHERAL_ENCODER));
    ASSERT(peripheral_start(PERIPHERAL_ENCODER) == ESP_OK);
    ASSERT(peripheral_is_running(PERIPHERAL_ENCODER));

    peripheral_stop(PERIPHERAL_ENCODER);
    ASSERT(!peripheral_is_running(PERIPHERAL_ENCODER));

    return 0;
}

TEST(input_events_are_queued_in_order)
{
    mock_peripheral_reset();

    ASSERT(peripheral_input_pending() == 0);

    peripheral_input_msg_t down = {
        .event = PERIPHERAL_INPUT_EVENT_BUTTON_DOWN,
        .source = PERIPHERAL_BUTTON,
        .value = 0,
    };
    peripheral_input_msg_t up = {
        .event = PERIPHERAL_INPUT_EVENT_BUTTON_UP,
        .source = PERIPHERAL_BUTTON,
        .value = 0,
    };

    mock_peripheral_push_input(down);
    mock_peripheral_push_input(up);
    ASSERT(peripheral_input_pending() == 2);

    peripheral_input_msg_t got;
    ASSERT(peripheral_input_pop(&got));
    ASSERT(got.event == PERIPHERAL_INPUT_EVENT_BUTTON_DOWN);

    ASSERT(peripheral_input_pop(&got));
    ASSERT(got.event == PERIPHERAL_INPUT_EVENT_BUTTON_UP);

    ASSERT(!peripheral_input_pop(&got));
    ASSERT(peripheral_input_pending() == 0);

    return 0;
}

TEST(input_event_names_are_readable)
{
    ASSERT(strcmp(peripheral_input_event_name(PERIPHERAL_INPUT_EVENT_BUTTON_DOWN),
                  "button down") == 0);
    ASSERT(strcmp(peripheral_input_event_name(PERIPHERAL_INPUT_EVENT_ENCODER_CW),
                  "encoder clockwise") == 0);
    ASSERT(strcmp(peripheral_input_event_name(PERIPHERAL_INPUT_EVENT_NONE),
                  "none") == 0);

    return 0;
}

TEST(font_glyphs_are_not_blank)
{
    // Every printable character has to have at least one pixel, otherwise
    // the display would show nothing for that character. The space is the
    // one exception, it is blank on purpose.
    for (int code = 33; code <= 126; ++code) {
        const uint8_t *glyph = font_get_glyph((char)code);
        bool any_set = false;

        for (int col = 0; col < FONT_GLYPH_WIDTH; ++col) {
            if (glyph[col] != 0) {
                any_set = true;
                break;
            }
        }

        if (!any_set) {
            printf("  FAIL: the glyph of '%c' is empty\n", (char)code);
            return 1;
        }
    }

    // An unknown character falls back to the question mark.
    const uint8_t *fallback = font_get_glyph('\x01');
    const uint8_t *question = font_get_glyph('?');
    ASSERT(memcmp(fallback, question, FONT_GLYPH_WIDTH) == 0);

    return 0;
}

TEST(font_line_width_is_reasonable)
{
    // A 128 pixel wide display fits 21 characters with a one pixel gap.
    ASSERT(font_chars_per_line(128) == 21);
    ASSERT(font_chars_per_line(0) == 0);

    return 0;
}

int test_peripheral_all(void)
{
    int failures = 0;

    printf("\nPeripheral Tests:\n");

    printf("  type_names_round_trip... ");
    if (test_type_names_round_trip() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  unknown_type_is_rejected... ");
    if (test_unknown_type_is_rejected() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  aliases_are_accepted... ");
    if (test_aliases_are_accepted() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  config_round_trip... ");
    if (test_config_round_trip() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  unconfigured_type_has_no_config... ");
    if (test_unconfigured_type_has_no_config() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  start_and_stop_are_tracked... ");
    if (test_start_and_stop_are_tracked() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  input_events_are_queued_in_order... ");
    if (test_input_events_are_queued_in_order() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  input_event_names_are_readable... ");
    if (test_input_event_names_are_readable() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  font_glyphs_are_not_blank... ");
    if (test_font_glyphs_are_not_blank() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  font_line_width_is_reasonable... ");
    if (test_font_line_width_is_reasonable() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    return failures;
}
