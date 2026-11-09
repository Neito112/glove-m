/*
 * Mock of the peripheral layer for host testing.
 *
 * The host tests never touch real hardware, so the layer is replaced by a
 * small in-memory version that records the calls.
 */

#ifndef MOCK_PERIPHERAL_H
#define MOCK_PERIPHERAL_H

#include "peripheral.h"
#include "peripheral_input.h"

// Forget the recorded state of the mock.
void mock_peripheral_reset(void);

// The number of times peripheral_set_config was called.
int mock_peripheral_set_count(void);

// The type that was stored last.
peripheral_type_t mock_peripheral_last_type(void);

// The configuration that was stored last.
const peripheral_config_t *mock_peripheral_last_config(void);

// Push an input event into the queue.
void mock_peripheral_push_input(peripheral_input_msg_t msg);

// The results the output drivers hand back.
void mock_peripheral_set_analog(int raw, int millivolts);
int mock_peripheral_get_analog_raw(void);
int mock_peripheral_get_analog_millivolts(void);
void mock_peripheral_set_mic_level(int level);
int mock_peripheral_get_mic_level(void);
void mock_peripheral_set_speaker_tone(int frequency, int duration);
int mock_peripheral_get_speaker_frequency(void);
int mock_peripheral_get_speaker_duration(void);
void mock_peripheral_set_display_text(const char *text);
const char *mock_peripheral_get_display_text(void);
int mock_peripheral_get_display_rows(void);

#endif // MOCK_PERIPHERAL_H
