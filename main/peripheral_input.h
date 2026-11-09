#ifndef PERIPHERAL_INPUT_H
#define PERIPHERAL_INPUT_H

#include "peripheral.h"

// Push buttons and rotary encoders.
//
// The encoder is decoded in software with a GPIO interrupt on both phases.
// That works on every target, including the ones without a pulse counter
// unit, and it is fast enough for a knob that a human turns.
//
// Every event is queued, so the chat and the agent can read them in order
// instead of racing against the interrupt.

typedef enum {
    PERIPHERAL_INPUT_EVENT_NONE = 0,
    PERIPHERAL_INPUT_EVENT_BUTTON_DOWN,
    PERIPHERAL_INPUT_EVENT_BUTTON_UP,
    PERIPHERAL_INPUT_EVENT_BUTTON_LONG_PRESS,
    PERIPHERAL_INPUT_EVENT_ENCODER_CW,
    PERIPHERAL_INPUT_EVENT_ENCODER_CCW,
    PERIPHERAL_INPUT_EVENT_ENCODER_BUTTON_DOWN,
    PERIPHERAL_INPUT_EVENT_ENCODER_BUTTON_UP,
} peripheral_input_event_t;

// The maximum number of events kept in the queue.
#define PERIPHERAL_INPUT_QUEUE_LENGTH 16

// The event together with the peripheral that produced it.
typedef struct {
    peripheral_input_event_t event;
    peripheral_type_t source;
    int32_t value; // the encoder counts the steps since the last read
} peripheral_input_msg_t;

// Configure a push button. The pin is taken from the stored configuration.
esp_err_t peripheral_button_start(const peripheral_config_t *config);
void peripheral_button_stop(void);
bool peripheral_button_is_running(void);

// Configure a rotary encoder with an optional push switch.
esp_err_t peripheral_encoder_start(const peripheral_config_t *config);
void peripheral_encoder_stop(void);
bool peripheral_encoder_is_running(void);

// Take the next queued event. Returns false when the queue is empty.
bool peripheral_input_pop(peripheral_input_msg_t *msg_out);

// The number of events waiting in the queue.
int peripheral_input_pending(void);

// The text of an event, e.g. "button down" or "encoder clockwise".
const char *peripheral_input_event_name(peripheral_input_event_t event);

// The total number of encoder steps counted since the last reset.
int32_t peripheral_encoder_total(void);

// Forget the encoder step counter.
void peripheral_encoder_reset_total(void);

#endif // PERIPHERAL_INPUT_H
