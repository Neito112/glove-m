#ifndef PERIPHERAL_H
#define PERIPHERAL_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Generic peripheral layer.
//
// Every piece of hardware the user can wire up is described by one
// peripheral_config_t. The configuration lives in NVS, so the assistant can
// set it up over a chat: the user says "put an OLED on SDA 4 and SCL 5",
// the agent calls peripheral_setup and the driver comes up right away.
//
// The drivers themselves stay independent of each other. A driver that the
// target does not support is compiled out instead of breaking the build.

typedef enum {
    PERIPHERAL_NONE = 0,
    PERIPHERAL_BUTTON,        // momentary push button
    PERIPHERAL_ENCODER,       // rotary encoder with a push switch
    PERIPHERAL_ANALOG,        // analog sensor on an ADC pin
    PERIPHERAL_DISPLAY_OLED,  // SSD1306 OLED over I2C
    PERIPHERAL_DISPLAY_LCD,   // ST7789 LCD over SPI
    PERIPHERAL_MIC,           // I2S microphone
    PERIPHERAL_SPEAKER,       // I2S amplifier with a speaker
    PERIPHERAL_TYPE_COUNT,
} peripheral_type_t;

// The configuration of every peripheral type in one struct. A driver only
// looks at the fields that belong to it, so the struct stays flat and needs
// no dynamic allocation.
typedef struct {
    int pin_a;          // button pin, encoder A, analog sensor input
    int pin_b;          // encoder B, I2S BCLK
    int pin_sw;         // encoder push switch
    int pin_sda;        // I2C data
    int pin_scl;        // I2C clock
    int pin_miso;       // SPI display
    int pin_mosi;       // SPI display
    int pin_dc;         // SPI display data/command
    int pin_cs;         // SPI display chip select
    int pin_clk;        // SPI display clock
    int pin_rst;        // display reset
    int pin_ws;         // I2S word select
    int pin_din;        // I2S data in
    int pin_dout;       // I2S data out
    int pin_amp_bclk;   // amplifier bit clock
    int pin_amp_lr;     // amplifier left/right
    int pin_amp_din;    // amplifier data in
    int address;        // I2C address of the device
    int frequency_hz;   // I2C or SPI clock
    int width;          // display width in pixels
    int height;         // display height in pixels
    bool active_low;    // the button pulls the pin low
    bool pullup;        // the button uses the internal pull-up
    bool inverted;      // the display colors are inverted
} peripheral_config_t;

// The name of a peripheral type, e.g. "button" or "oled".
const char *peripheral_type_name(peripheral_type_t type);

// Parse a type name. Returns false when the name is unknown.
bool peripheral_type_from_name(const char *name, peripheral_type_t *type_out);

// Read the configuration of one type from NVS. Returns false when the type
// was never configured.
bool peripheral_get_config(peripheral_type_t type, peripheral_config_t *config_out);

// Write the configuration of one type to NVS.
esp_err_t peripheral_set_config(peripheral_type_t type, const peripheral_config_t *config);

// Remove the configuration of one type from NVS.
esp_err_t peripheral_remove_config(peripheral_type_t type);

// Bring the driver of one type up with the stored configuration. Returns
// ESP_ERR_NOT_FOUND when the type was never configured.
esp_err_t peripheral_start(peripheral_type_t type);

// Bring the driver of one type down again.
void peripheral_stop(peripheral_type_t type);

// True when the driver of one type is running.
bool peripheral_is_running(peripheral_type_t type);

// Start every configured peripheral. Called once the radio is up.
void peripheral_start_all(void);

// Stop every running peripheral.
void peripheral_stop_all(void);

// List the configured peripherals as a readable multi line report.
void peripheral_report(char *buf, size_t buf_len);

// The number of configured peripheral types.
int peripheral_configured_count(void);

#endif // PERIPHERAL_H
