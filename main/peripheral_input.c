#include "peripheral_input.h"
#include "config.h"
#include "gpio_policy.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "periph_input";

// The queue of input events. It is created on the first start.
static QueueHandle_t s_event_queue = NULL;

static bool s_button_running = false;
static bool s_encoder_running = false;

static gpio_num_t s_button_pin = GPIO_NUM_NC;
static gpio_num_t s_encoder_a = GPIO_NUM_NC;
static gpio_num_t s_encoder_b = GPIO_NUM_NC;
static gpio_num_t s_encoder_sw = GPIO_NUM_NC;

// The quadrature state of the encoder, used to decide the direction.
static volatile int32_t s_encoder_total = 0;
static volatile uint8_t s_encoder_last_state = 0;

// The button uses a timer to detect a long press.
#define BUTTON_LONG_PRESS_MS 800
#define BUTTON_DEBOUNCE_MS 25

static int64_t s_button_down_at_us = 0;
static bool s_button_is_down = false;
static bool s_encoder_sw_is_down = false;

static void input_push_event(peripheral_input_event_t event, peripheral_type_t source, int32_t value)
{
    if (!s_event_queue) {
        return;
    }

    peripheral_input_msg_t msg = {
        .event = event,
        .source = source,
        .value = value,
    };

    // The queue is small on purpose. When it is full the oldest event is
    // dropped so a burst of encoder steps cannot block the interrupt.
    if (xQueueSend(s_event_queue, &msg, 0) != pdTRUE) {
        peripheral_input_msg_t dropped;
        if (xQueueReceive(s_event_queue, &dropped, 0) == pdTRUE) {
            xQueueSend(s_event_queue, &msg, 0);
        }
    }
}

static void button_timer_callback(void *arg)
{
    (void)arg;

    if (!s_button_is_down) {
        return;
    }

    int64_t now = esp_timer_get_time();
    if (now - s_button_down_at_us >= (int64_t)BUTTON_LONG_PRESS_MS * 1000) {
        // The press is reported once. The release still arrives later.
        s_button_is_down = false;
        input_push_event(PERIPHERAL_INPUT_EVENT_BUTTON_LONG_PRESS,
                         PERIPHERAL_BUTTON, 0);
    }
}

// The timer that watches the button for a long press.
static esp_timer_handle_t s_button_timer = NULL;

static void IRAM_ATTR button_isr(void *arg)
{
    (void)arg;

    int level = gpio_get_level(s_button_pin);
    int64_t now = esp_timer_get_time();

    if (level) {
        // Released.
        if (s_button_is_down) {
            s_button_is_down = false;
            input_push_event(PERIPHERAL_INPUT_EVENT_BUTTON_UP, PERIPHERAL_BUTTON, 0);
        }
    } else {
        // Pressed.
        if (!s_button_is_down) {
            s_button_is_down = true;
            s_button_down_at_us = now;
            input_push_event(PERIPHERAL_INPUT_EVENT_BUTTON_DOWN, PERIPHERAL_BUTTON, 0);
        }
    }
}

static void IRAM_ATTR encoder_isr(void *arg)
{
    (void)arg;

    uint8_t a = gpio_get_level(s_encoder_a);
    uint8_t b = gpio_get_level(s_encoder_b);
    uint8_t state = (uint8_t)((a << 1) | b);

    // The standard quadrature decoding table. A transition from one state
    // to the next that is not in the table means the knob was turned too
    // fast, so the step is ignored.
    static const int8_t s_quad_table[16] = {
        0, -1, 1, 0,
        1, 0, 0, -1,
        -1, 0, 0, 1,
        0, 1, -1, 0,
    };

    int8_t delta = s_quad_table[(s_encoder_last_state << 2) | state];
    s_encoder_last_state = state;

    if (delta > 0) {
        s_encoder_total++;
        input_push_event(PERIPHERAL_INPUT_EVENT_ENCODER_CW, PERIPHERAL_ENCODER, 1);
    } else if (delta < 0) {
        s_encoder_total--;
        input_push_event(PERIPHERAL_INPUT_EVENT_ENCODER_CCW, PERIPHERAL_ENCODER, -1);
    }
}

static void IRAM_ATTR encoder_sw_isr(void *arg)
{
    (void)arg;

    int level = gpio_get_level(s_encoder_sw);

    if (level) {
        if (s_encoder_sw_is_down) {
            s_encoder_sw_is_down = false;
            input_push_event(PERIPHERAL_INPUT_EVENT_ENCODER_BUTTON_UP,
                             PERIPHERAL_ENCODER, 0);
        }
    } else {
        if (!s_encoder_sw_is_down) {
            s_encoder_sw_is_down = true;
            input_push_event(PERIPHERAL_INPUT_EVENT_ENCODER_BUTTON_DOWN,
                             PERIPHERAL_ENCODER, 0);
        }
    }
}

static esp_err_t ensure_queue(void)
{
    if (s_event_queue) {
        return ESP_OK;
    }

    s_event_queue = xQueueCreate(PERIPHERAL_INPUT_QUEUE_LENGTH,
                                  sizeof(peripheral_input_msg_t));
    return s_event_queue ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t peripheral_button_start(const peripheral_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!gpio_policy_pin_is_allowed(config->pin_a)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensure_queue();
    if (err != ESP_OK) {
        return err;
    }

    peripheral_button_stop();

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << config->pin_a,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = config->pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };

    err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure the button pin %d", config->pin_a);
        return err;
    }

    s_button_pin = (gpio_num_t)config->pin_a;
    gpio_install_isr_service(0);
    gpio_isr_handler_add(s_button_pin, button_isr, NULL);

    if (!s_button_timer) {
        esp_timer_create_args_t timer_args = {
            .callback = button_timer_callback,
            .dispatch_method = ESP_TIMER_TASK,
        };
        esp_timer_create(&timer_args, &s_button_timer);
    }
    esp_timer_start_periodic(s_button_timer,
                             (uint32_t)(BUTTON_LONG_PRESS_MS / 4) * 1000);

    s_button_running = true;
    ESP_LOGI(TAG, "Button is ready on GPIO %d", config->pin_a);
    return ESP_OK;
}

void peripheral_button_stop(void)
{
    if (!s_button_running) {
        return;
    }

    gpio_isr_handler_remove(s_button_pin);
    gpio_set_level(s_button_pin, 0);
    gpio_reset_pin(s_button_pin);
    if (s_button_timer) {
        esp_timer_stop(s_button_timer);
    }

    s_button_running = false;
    s_button_pin = GPIO_NUM_NC;
    ESP_LOGI(TAG, "Button stopped");
}

bool peripheral_button_is_running(void)
{
    return s_button_running;
}

esp_err_t peripheral_encoder_start(const peripheral_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!gpio_policy_pin_is_allowed(config->pin_a) ||
        !gpio_policy_pin_is_allowed(config->pin_b)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensure_queue();
    if (err != ESP_OK) {
        return err;
    }

    peripheral_encoder_stop();

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << config->pin_a) | (1ULL << config->pin_b),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };

    err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure the encoder pins");
        return err;
    }

    s_encoder_a = (gpio_num_t)config->pin_a;
    s_encoder_b = (gpio_num_t)config->pin_b;

    s_encoder_last_state = (uint8_t)((gpio_get_level(s_encoder_a) << 1) |
                                      gpio_get_level(s_encoder_b));

    gpio_install_isr_service(0);
    gpio_isr_handler_add(s_encoder_a, encoder_isr, NULL);
    gpio_isr_handler_add(s_encoder_b, encoder_isr, NULL);

    // The push switch of the encoder is optional.
    if (config->pin_sw >= 0 && gpio_policy_pin_is_allowed(config->pin_sw)) {
        gpio_config_t sw_conf = {
            .pin_bit_mask = 1ULL << config->pin_sw,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_ANYEDGE,
        };
        err = gpio_config(&sw_conf);
        if (err == ESP_OK) {
            s_encoder_sw = (gpio_num_t)config->pin_sw;
            gpio_isr_handler_add(s_encoder_sw, encoder_sw_isr, NULL);
        }
    }

    s_encoder_running = true;
    ESP_LOGI(TAG, "Encoder is ready on GPIO A=%d B=%d", config->pin_a, config->pin_b);
    return ESP_OK;
}

void peripheral_encoder_stop(void)
{
    if (!s_encoder_running) {
        return;
    }

    gpio_isr_handler_remove(s_encoder_a);
    gpio_reset_pin(s_encoder_a);
    s_encoder_a = GPIO_NUM_NC;

    gpio_isr_handler_remove(s_encoder_b);
    gpio_reset_pin(s_encoder_b);
    s_encoder_b = GPIO_NUM_NC;

    if (s_encoder_sw != GPIO_NUM_NC) {
        gpio_isr_handler_remove(s_encoder_sw);
        gpio_reset_pin(s_encoder_sw);
        s_encoder_sw = GPIO_NUM_NC;
    }

    s_encoder_running = false;
    ESP_LOGI(TAG, "Encoder stopped");
}

bool peripheral_encoder_is_running(void)
{
    return s_encoder_running;
}

bool peripheral_input_pop(peripheral_input_msg_t *msg_out)
{
    if (!s_event_queue || !msg_out) {
        return false;
    }
    return xQueueReceive(s_event_queue, msg_out, 0) == pdTRUE;
}

int peripheral_input_pending(void)
{
    if (!s_event_queue) {
        return 0;
    }
    return (int)uxQueueMessagesWaiting(s_event_queue);
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

int32_t peripheral_encoder_total(void)
{
    return s_encoder_total;
}

void peripheral_encoder_reset_total(void)
{
    s_encoder_total = 0;
}
