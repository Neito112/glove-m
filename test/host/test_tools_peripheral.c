/*
 * Host tests for the peripheral tool handlers.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <cjson/cJSON.h>

#include "mock_peripheral.h"
#include "peripheral.h"
#include "tools_handlers.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)
#define ASSERT_STR_CONTAINS(haystack, needle) do { \
    if (strstr((haystack), (needle)) == NULL) { \
        printf("  FAIL: expected substring '%s' in '%s' (line %d)\n", (needle), (haystack), __LINE__); \
        return 1; \
    } \
} while(0)

TEST(setup_requires_a_type)
{
    cJSON *input = cJSON_Parse("{}");
    char result[256] = {0};

    ASSERT(input != NULL);
    mock_peripheral_reset();

    ASSERT(!tools_peripheral_setup_handler(input, result, sizeof(result)));
    ASSERT_STR_CONTAINS(result, "'type' required");

    cJSON_Delete(input);
    return 0;
}

TEST(setup_rejects_an_unknown_type)
{
    cJSON *input = cJSON_Parse("{\"type\":\"floppy_disk\"}");
    char result[256] = {0};

    ASSERT(input != NULL);
    mock_peripheral_reset();

    ASSERT(!tools_peripheral_setup_handler(input, result, sizeof(result)));
    ASSERT_STR_CONTAINS(result, "unknown peripheral type");
    ASSERT(mock_peripheral_set_count() == 0);

    cJSON_Delete(input);
    return 0;
}

TEST(setup_stores_the_button_pin)
{
    cJSON *input = cJSON_Parse("{\"type\":\"button\",\"pin\":5}");
    char result[256] = {0};

    ASSERT(input != NULL);
    mock_peripheral_reset();

    ASSERT(tools_peripheral_setup_handler(input, result, sizeof(result)));
    ASSERT_STR_CONTAINS(result, "button");
    ASSERT(mock_peripheral_set_count() == 1);
    ASSERT(mock_peripheral_last_type() == PERIPHERAL_BUTTON);
    ASSERT(mock_peripheral_last_config()->pin_a == 5);

    cJSON_Delete(input);
    return 0;
}

TEST(setup_stores_the_encoder_pins)
{
    cJSON *input = cJSON_Parse("{\"type\":\"encoder\",\"pin_a\":4,\"pin_b\":5,\"pin_sw\":6}");
    char result[256] = {0};

    ASSERT(input != NULL);
    mock_peripheral_reset();

    ASSERT(tools_peripheral_setup_handler(input, result, sizeof(result)));
    ASSERT(mock_peripheral_last_type() == PERIPHERAL_ENCODER);
    ASSERT(mock_peripheral_last_config()->pin_a == 4);
    ASSERT(mock_peripheral_last_config()->pin_b == 5);
    ASSERT(mock_peripheral_last_config()->pin_sw == 6);

    cJSON_Delete(input);
    return 0;
}

TEST(setup_stores_the_oled_wiring)
{
    cJSON *input = cJSON_Parse(
        "{\"type\":\"oled\",\"pin_sda\":4,\"pin_scl\":5,\"address\":60,\"width\":128,\"height\":64}");
    char result[256] = {0};

    ASSERT(input != NULL);
    mock_peripheral_reset();

    ASSERT(tools_peripheral_setup_handler(input, result, sizeof(result)));
    ASSERT(mock_peripheral_last_type() == PERIPHERAL_DISPLAY_OLED);
    ASSERT(mock_peripheral_last_config()->pin_sda == 4);
    ASSERT(mock_peripheral_last_config()->pin_scl == 5);
    ASSERT(mock_peripheral_last_config()->address == 60);

    cJSON_Delete(input);
    return 0;
}

TEST(setup_stores_the_microphone_wiring)
{
    cJSON *input = cJSON_Parse("{\"type\":\"mic\",\"pin_b\":2,\"pin_ws\":3,\"pin_din\":4}");
    char result[256] = {0};

    ASSERT(input != NULL);
    mock_peripheral_reset();

    ASSERT(tools_peripheral_setup_handler(input, result, sizeof(result)));
    ASSERT(mock_peripheral_last_type() == PERIPHERAL_MIC);
    ASSERT(mock_peripheral_last_config()->pin_b == 2);
    ASSERT(mock_peripheral_last_config()->pin_ws == 3);
    ASSERT(mock_peripheral_last_config()->pin_din == 4);

    cJSON_Delete(input);
    return 0;
}

TEST(setup_rejects_a_pin_outside_the_policy)
{
    cJSON *input = cJSON_Parse("{\"type\":\"button\",\"pin\":1}");
    char result[256] = {0};

    ASSERT(input != NULL);
    mock_peripheral_reset();

    ASSERT(!tools_peripheral_setup_handler(input, result, sizeof(result)));
    ASSERT_STR_CONTAINS(result, "not allowed");
    ASSERT(mock_peripheral_set_count() == 0);

    cJSON_Delete(input);
    return 0;
}

TEST(setup_keeps_the_pins_that_were_not_mentioned)
{
    cJSON *first = cJSON_Parse("{\"type\":\"encoder\",\"pin_a\":4,\"pin_b\":5}");
    cJSON *second = cJSON_Parse("{\"type\":\"encoder\",\"pin_sw\":6}");
    char result[256] = {0};

    ASSERT(first != NULL);
    ASSERT(second != NULL);
    mock_peripheral_reset();

    ASSERT(tools_peripheral_setup_handler(first, result, sizeof(result)));
    ASSERT(tools_peripheral_setup_handler(second, result, sizeof(result)));

    ASSERT(mock_peripheral_last_config()->pin_a == 4);
    ASSERT(mock_peripheral_last_config()->pin_b == 5);
    ASSERT(mock_peripheral_last_config()->pin_sw == 6);

    cJSON_Delete(first);
    cJSON_Delete(second);
    return 0;
}

TEST(display_tool_writes_the_text)
{
    cJSON *input = cJSON_Parse("{\"text\":\"hello\",\"row\":2}");
    char result[256] = {0};

    ASSERT(input != NULL);
    mock_peripheral_reset();
    peripheral_start(PERIPHERAL_DISPLAY_OLED);

    ASSERT(tools_peripheral_display_handler(input, result, sizeof(result)));
    ASSERT_STR_CONTAINS(result, "hello");
    ASSERT_STR_CONTAINS(result, "row 2");
    ASSERT(strcmp(mock_peripheral_get_display_text(), "hello") == 0);

    cJSON_Delete(input);
    return 0;
}

TEST(display_tool_clears_the_screen)
{
    cJSON *input = cJSON_Parse("{\"clear\":true}");
    char result[256] = {0};

    ASSERT(input != NULL);
    mock_peripheral_reset();
    peripheral_start(PERIPHERAL_DISPLAY_OLED);
    mock_peripheral_set_display_text("old");

    ASSERT(tools_peripheral_display_handler(input, result, sizeof(result)));
    ASSERT_STR_CONTAINS(result, "cleared");
    ASSERT(mock_peripheral_get_display_text()[0] == '\0');

    cJSON_Delete(input);
    return 0;
}

TEST(analog_tool_reports_the_reading)
{
    cJSON *input = cJSON_Parse("{}");
    char result[256] = {0};

    ASSERT(input != NULL);
    mock_peripheral_reset();
    peripheral_set_config(PERIPHERAL_ANALOG, &(peripheral_config_t){.pin_a = 4});
    mock_peripheral_set_analog(2048, 1650);

    ASSERT(tools_peripheral_analog_handler(input, result, sizeof(result)));
    ASSERT_STR_CONTAINS(result, "2048");
    ASSERT_STR_CONTAINS(result, "1650");

    cJSON_Delete(input);
    return 0;
}

TEST(speaker_tool_plays_the_tone)
{
    cJSON *input = cJSON_Parse("{\"frequency_hz\":880,\"duration_ms\":150}");
    char result[256] = {0};

    ASSERT(input != NULL);
    mock_peripheral_reset();
    peripheral_start(PERIPHERAL_SPEAKER);

    ASSERT(tools_peripheral_speaker_handler(input, result, sizeof(result)));
    ASSERT(mock_peripheral_get_speaker_frequency() == 880);
    ASSERT(mock_peripheral_get_speaker_duration() == 150);

    cJSON_Delete(input);
    return 0;
}

TEST(mic_tool_reports_the_level)
{
    cJSON *input = cJSON_Parse("{}");
    char result[256] = {0};

    ASSERT(input != NULL);
    mock_peripheral_reset();
    peripheral_start(PERIPHERAL_MIC);
    mock_peripheral_set_mic_level(4096);

    ASSERT(tools_peripheral_mic_handler(input, result, sizeof(result)));
    ASSERT_STR_CONTAINS(result, "4096");

    cJSON_Delete(input);
    return 0;
}

TEST(input_tool_lists_the_events)
{
    cJSON *input = cJSON_Parse("{}");
    char result[256] = {0};

    ASSERT(input != NULL);
    mock_peripheral_reset();

    peripheral_input_msg_t msg = {
        .event = PERIPHERAL_INPUT_EVENT_BUTTON_DOWN,
        .source = PERIPHERAL_BUTTON,
        .value = 0,
    };
    mock_peripheral_push_input(msg);

    ASSERT(tools_peripheral_input_handler(input, result, sizeof(result)));
    ASSERT_STR_CONTAINS(result, "button down");

    cJSON_Delete(input);
    return 0;
}

TEST(input_tool_reports_an_empty_queue)
{
    cJSON *input = cJSON_Parse("{}");
    char result[256] = {0};

    ASSERT(input != NULL);
    mock_peripheral_reset();

    ASSERT(tools_peripheral_input_handler(input, result, sizeof(result)));
    ASSERT_STR_CONTAINS(result, "No input event");

    cJSON_Delete(input);
    return 0;
}

TEST(remove_tool_clears_the_configuration)
{
    cJSON *input = cJSON_Parse("{\"type\":\"button\"}");
    char result[256] = {0};

    ASSERT(input != NULL);
    mock_peripheral_reset();
    peripheral_set_config(PERIPHERAL_BUTTON, &(peripheral_config_t){.pin_a = 5});

    ASSERT(tools_peripheral_remove_handler(input, result, sizeof(result)));
    ASSERT_STR_CONTAINS(result, "removed");
    ASSERT(mock_peripheral_last_type() == PERIPHERAL_NONE);

    cJSON_Delete(input);
    return 0;
}

TEST(list_tool_reports_the_configuration)
{
    cJSON *input = cJSON_Parse("{}");
    char result[256] = {0};

    ASSERT(input != NULL);
    mock_peripheral_reset();
    peripheral_set_config(PERIPHERAL_ENCODER, &(peripheral_config_t){.pin_a = 5});

    ASSERT(tools_peripheral_list_handler(input, result, sizeof(result)));
    ASSERT_STR_CONTAINS(result, "encoder");

    cJSON_Delete(input);
    return 0;
}

int test_tools_peripheral_all(void)
{
    int failures = 0;

    printf("\nPeripheral Tool Tests:\n");

#define RUN(name) \
    printf("  %s... ", #name); \
    if (test_##name() == 0) { \
        printf("OK\n"); \
    } else { \
        failures++; \
    }

    RUN(setup_requires_a_type)
    RUN(setup_rejects_an_unknown_type)
    RUN(setup_stores_the_button_pin)
    RUN(setup_stores_the_encoder_pins)
    RUN(setup_stores_the_oled_wiring)
    RUN(setup_stores_the_microphone_wiring)
    RUN(setup_rejects_a_pin_outside_the_policy)
    RUN(setup_keeps_the_pins_that_were_not_mentioned)
    RUN(display_tool_writes_the_text)
    RUN(display_tool_clears_the_screen)
    RUN(analog_tool_reports_the_reading)
    RUN(speaker_tool_plays_the_tone)
    RUN(mic_tool_reports_the_level)
    RUN(input_tool_lists_the_events)
    RUN(input_tool_reports_an_empty_queue)
    RUN(remove_tool_clears_the_configuration)
    RUN(list_tool_reports_the_configuration)

#undef RUN

    return failures;
}
