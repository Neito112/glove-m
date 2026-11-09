#ifndef DRIVER_GPIO_H
#define DRIVER_GPIO_H

#include <stdint.h>

typedef int gpio_num_t;

#define GPIO_MODE_OUTPUT 1
#define GPIO_MODE_INPUT 2
#define GPIO_MODE_INPUT_OUTPUT (GPIO_MODE_OUTPUT | GPIO_MODE_INPUT)
#define GPIO_IS_VALID_GPIO(pin) ((pin) >= 0 && (pin) < GPIO_TEST_PIN_LIMIT)

#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_PULLDOWN_ENABLE 1
#define GPIO_INTR_DISABLE 0

typedef struct {
    uint64_t pin_bit_mask;
    int mode;
    int pull_up_en;
    int pull_down_en;
    int intr_type;
} gpio_config_t;

enum {
    GPIO_TEST_PIN_LIMIT = 128,
};

static int s_gpio_test_mode[GPIO_TEST_PIN_LIMIT];
static int s_gpio_test_level[GPIO_TEST_PIN_LIMIT];

static inline int gpio_config(const gpio_config_t *conf)
{
    if (!conf) {
        return 0;
    }
    for (int i = 0; i < GPIO_TEST_PIN_LIMIT; i++) {
        if (conf->pin_bit_mask & (1ULL << i)) {
            s_gpio_test_mode[i] = conf->mode;
        }
    }
    return 0;
}

static inline int gpio_reset_pin(gpio_num_t pin)
{
    if (pin >= 0 && pin < GPIO_TEST_PIN_LIMIT) {
        s_gpio_test_mode[pin] = 0;
        s_gpio_test_level[pin] = 0;
    }
    return 0;
}

static inline int gpio_set_direction(gpio_num_t pin, int mode)
{
    if (pin >= 0 && pin < GPIO_TEST_PIN_LIMIT) {
        s_gpio_test_mode[pin] = mode;
    }
    return 0;
}

static inline int gpio_input_enable(gpio_num_t pin)
{
    if (pin >= 0 && pin < GPIO_TEST_PIN_LIMIT) {
        s_gpio_test_mode[pin] |= GPIO_MODE_INPUT;
    }
    return 0;
}

static inline int gpio_set_level(gpio_num_t pin, int level)
{
    if (pin >= 0 && pin < GPIO_TEST_PIN_LIMIT) {
        s_gpio_test_level[pin] = level ? 1 : 0;
    }
    return 0;
}

static inline int gpio_get_level(gpio_num_t pin)
{
    if (pin < 0 || pin >= GPIO_TEST_PIN_LIMIT) {
        return 0;
    }
    if ((s_gpio_test_mode[pin] & GPIO_MODE_INPUT) == 0) {
        return 0;
    }
    return s_gpio_test_level[pin];
}

static inline void gpio_test_reset_state(void)
{
    for (int i = 0; i < GPIO_TEST_PIN_LIMIT; i++) {
        s_gpio_test_mode[i] = 0;
        s_gpio_test_level[i] = 0;
    }
}

#endif // DRIVER_GPIO_H
