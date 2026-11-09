#ifndef PERIPHERAL_DISPLAY_H
#define PERIPHERAL_DISPLAY_H

#include "peripheral.h"

// Displays.
//
// Two panels are supported:
//   - the SSD1306 OLED over I2C, the usual choice for a small status display,
//   - the ST7789 LCD over SPI, for a larger color screen.
//
// Both share the same interface, so the caller does not care which one is
// wired up. The frame buffer lives in RAM, so a full screen costs
// width * height / 8 bytes. On a target without PSRAM that budget is small,
// which is why the frame buffer size is capped.

#define PERIPHERAL_DISPLAY_MAX_BUFFER 32768

// The state of a display driver.
typedef struct {
    int width;
    int height;
    bool color;   // true for the RGB LCD, false for the monochrome OLED
} peripheral_display_info_t;

// Bring a display up with the stored configuration.
esp_err_t peripheral_display_start(const peripheral_config_t *config);
void peripheral_display_stop(void);
bool peripheral_display_is_running(void);

// The geometry of the running display.
bool peripheral_display_get_info(peripheral_display_info_t *info_out);

// Draw a text line. The text is clipped to the display.
esp_err_t peripheral_display_write_line(int row, const char *text);

// Clear the whole display.
esp_err_t peripheral_display_clear(void);

// Set the brightness of the display, 0-100.
esp_err_t peripheral_display_set_brightness(int brightness);

// The number of text rows that fit on the display.
int peripheral_display_rows(void);

#endif // PERIPHERAL_DISPLAY_H
