#include "peripheral_display.h"
#include "config.h"
#include "gpio_policy.h"
#include "peripheral_font.h"

#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "periph_display";

// The display is either the SSD1306 OLED over I2C or the ST7789 LCD over
// SPI. Both are driven through the esp_lcd component.
static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_panel_io_handle_t s_io = NULL;
static bool s_display_running = false;
static bool s_is_color = false;

// The frame buffer. The OLED uses one bit per pixel, the LCD uses two bytes.
static uint8_t *s_buffer = NULL;
static size_t s_buffer_size = 0;
static int s_width = 0;
static int s_height = 0;

#define OLED_DEFAULT_WIDTH 128
#define OLED_DEFAULT_HEIGHT 64
#define LCD_DEFAULT_WIDTH 240
#define LCD_DEFAULT_HEIGHT 240

// The SSD1306 I2C address. 0x3C is the common one, 0x3D the alternative.
#define SSD1306_I2C_ADDR_PRIMARY 0x3C
#define SSD1306_I2C_ADDR_SECONDARY 0x3D

static void display_release(void)
{
    if (s_panel) {
        esp_lcd_panel_del(s_panel);
        s_panel = NULL;
    }
    if (s_io) {
        esp_lcd_panel_io_del(s_io);
        s_io = NULL;
    }
    if (s_buffer) {
        free(s_buffer);
        s_buffer = NULL;
    }
    s_buffer_size = 0;
}

static esp_err_t display_start_oled(const peripheral_config_t *config)
{
    int address = config->address > 0 ? config->address : SSD1306_I2C_ADDR_PRIMARY;
    if (address != SSD1306_I2C_ADDR_PRIMARY && address != SSD1306_I2C_ADDR_SECONDARY) {
        ESP_LOGE(TAG, "The OLED address must be 0x3C or 0x3D");
        return ESP_ERR_INVALID_ARG;
    }

    i2c_master_bus_handle_t bus = NULL;
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = config->pin_sda,
        .scl_io_num = config->pin_scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&bus_config, &bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create the I2C bus for the OLED");
        return err;
    }

    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = address,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };

    err = esp_lcd_new_panel_io_i2c(bus, &io_config, &s_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create the OLED I2C IO");
        i2c_del_master_bus(bus);
        return err;
    }

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = config->pin_rst,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 1,
    };

    err = esp_lcd_new_panel_ssd1306(s_io, &panel_config, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create the SSD1306 panel");
        display_release();
        i2c_del_master_bus(bus);
        return err;
    }

    err = esp_lcd_panel_reset(s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset the SSD1306 panel");
        display_release();
        i2c_del_master_bus(bus);
        return err;
    }

    err = esp_lcd_panel_init(s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize the SSD1306 panel");
        display_release();
        i2c_del_master_bus(bus);
        return err;
    }

    if (config->inverted) {
        esp_lcd_panel_invert_color(s_panel, true);
    }

    // The I2C bus handle belongs to the panel from now on.
    s_is_color = false;
    s_width = config->width > 0 ? config->width : OLED_DEFAULT_WIDTH;
    s_height = config->height > 0 ? config->height : OLED_DEFAULT_HEIGHT;

    ESP_LOGI(TAG, "OLED is ready on SDA=%d SCL=%d, %dx%d",
             config->pin_sda, config->pin_scl, s_width, s_height);
    return ESP_OK;
}

static esp_err_t display_start_lcd(const peripheral_config_t *config)
{
    spi_bus_config_t bus_config = {
        .mosi_io_num = config->pin_mosi,
        .miso_io_num = -1,
        .sclk_io_num = config->pin_clk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (size_t)(config->width > 0 ? config->width : LCD_DEFAULT_WIDTH) * 80,
    };

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize the SPI bus for the LCD");
        return err;
    }

    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = config->pin_cs,
        .dc_gpio_num = config->pin_dc,
        .spi_mode = 0,
        .pclk_hz = config->frequency_hz > 0 ? (unsigned int)config->frequency_hz : (40 * 1000 * 1000),
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .flags.dc_high_on_cmd = 1,
        .flags.dc_low_on_data = 1,
        .flags.dc_low_on_param = 1,
    };

    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &s_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create the LCD SPI IO");
        spi_bus_free(SPI2_HOST);
        return err;
    }

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = config->pin_rst,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };

    err = esp_lcd_new_panel_st7789(s_io, &panel_config, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create the ST7789 panel");
        display_release();
        spi_bus_free(SPI2_HOST);
        return err;
    }

    err = esp_lcd_panel_reset(s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset the ST7789 panel");
        display_release();
        spi_bus_free(SPI2_HOST);
        return err;
    }

    err = esp_lcd_panel_init(s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize the ST7789 panel");
        display_release();
        spi_bus_free(SPI2_HOST);
        return err;
    }

    if (config->inverted) {
        esp_lcd_panel_invert_color(s_panel, true);
    }

    s_is_color = true;
    s_width = config->width > 0 ? config->width : LCD_DEFAULT_WIDTH;
    s_height = config->height > 0 ? config->height : LCD_DEFAULT_HEIGHT;

    ESP_LOGI(TAG, "LCD is ready on MOSI=%d CLK=%d, %dx%d",
             config->pin_mosi, config->pin_clk, s_width, s_height);
    return ESP_OK;
}

esp_err_t peripheral_display_start(const peripheral_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    peripheral_display_stop();

    // The frame buffer holds one bit per pixel for the OLED and two bytes
    // per pixel for the LCD. A full LCD frame does not fit on a target
    // without PSRAM, so the buffer is capped and the display is rejected
    // when the geometry does not fit.
    size_t pixels = (size_t)(config->width > 0 ? config->width : OLED_DEFAULT_WIDTH) *
                    (size_t)(config->height > 0 ? config->height : OLED_DEFAULT_HEIGHT);
    size_t needed = pixels / 8;

    if (needed > PERIPHERAL_DISPLAY_MAX_BUFFER) {
        ESP_LOGE(TAG, "The display is too large for the RAM of this target");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = config->pin_mosi >= 0 ? display_start_lcd(config)
                                          : display_start_oled(config);
    if (err != ESP_OK) {
        return err;
    }

    needed = s_is_color ? pixels * 2 : pixels / 8;
    s_buffer = calloc(1, needed);
    if (!s_buffer) {
        ESP_LOGE(TAG, "Failed to allocate the display frame buffer");
        display_release();
        return ESP_ERR_NO_MEM;
    }
    s_buffer_size = needed;

    s_display_running = true;
    return ESP_OK;
}

void peripheral_display_stop(void)
{
    if (!s_display_running) {
        return;
    }

    display_release();
    s_display_running = false;
    s_width = 0;
    s_height = 0;
    ESP_LOGI(TAG, "Display stopped");
}

bool peripheral_display_is_running(void)
{
    return s_display_running;
}

bool peripheral_display_get_info(peripheral_display_info_t *info_out)
{
    if (!s_display_running || !info_out) {
        return false;
    }

    info_out->width = s_width;
    info_out->height = s_height;
    info_out->color = s_is_color;
    return true;
}

esp_err_t peripheral_display_clear(void)
{
    if (!s_display_running) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(s_buffer, 0, s_buffer_size);
    return esp_lcd_panel_draw_bitmap(s_panel, 0, 0, s_width, s_height, s_buffer);
}

esp_err_t peripheral_display_write_line(int row, const char *text)
{
    if (!s_display_running || !text) {
        return ESP_ERR_INVALID_STATE;
    }

    int line_height = FONT_GLYPH_HEIGHT + 1;
    int y = row * line_height;
    if (y + line_height > s_height) {
        return ESP_ERR_INVALID_ARG;
    }

    int max_chars = font_chars_per_line(s_width);
    int len = (int)strlen(text);
    if (len > max_chars) {
        len = max_chars;
    }

    if (s_is_color) {
        // The LCD uses two bytes per pixel in RGB565.
        uint8_t *pixels = calloc(1, (size_t)s_width * s_height * 2);
        if (!pixels) {
            return ESP_ERR_NO_MEM;
        }

        for (int i = 0; i < len; ++i) {
            const uint8_t *glyph = font_get_glyph(text[i]);
            for (int col = 0; col < FONT_GLYPH_WIDTH; ++col) {
                for (int row_bit = 0; row_bit < FONT_GLYPH_HEIGHT; ++row_bit) {
                    if (!((glyph[col] >> row_bit) & 1)) {
                        continue;
                    }
                    int px = i * (FONT_GLYPH_WIDTH + 1) + col;
                    int py = y + row_bit;
                    if (px >= s_width || py >= s_height) {
                        continue;
                    }
                    size_t offset = ((size_t)py * s_width + px) * 2;
                    pixels[offset] = 0xFF;
                    pixels[offset + 1] = 0xFF;
                }
            }
        }

        esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, y, s_width, y + line_height, pixels);
        free(pixels);
        return err;
    }

    // The OLED uses one bit per pixel, the rows are padded to a byte.
    memset(s_buffer, 0, s_buffer_size);
    int bytes_per_row = (s_width + 7) / 8;

    for (int i = 0; i < len; ++i) {
        const uint8_t *glyph = font_get_glyph(text[i]);
        for (int col = 0; col < FONT_GLYPH_WIDTH; ++col) {
            for (int row_bit = 0; row_bit < FONT_GLYPH_HEIGHT; ++row_bit) {
                if (!((glyph[col] >> row_bit) & 1)) {
                    continue;
                }
                int px = i * (FONT_GLYPH_WIDTH + 1) + col;
                int py = y + row_bit;
                if (px >= s_width || py >= s_height) {
                    continue;
                }
                size_t offset = (size_t)py * bytes_per_row + (px / 8);
                s_buffer[offset] |= 1 << (px % 8);
            }
        }
    }

    return esp_lcd_panel_draw_bitmap(s_panel, 0, y, s_width, y + line_height, s_buffer);
}

esp_err_t peripheral_display_set_brightness(int brightness)
{
    if (!s_display_running) {
        return ESP_ERR_INVALID_STATE;
    }

    // The panel driver has no backlight control on its own. A display with
    // a backlight usually drives it from a GPIO, so the value is applied
    // through the regular GPIO path when one is configured.
    (void)brightness;
    return ESP_OK;
}

int peripheral_display_rows(void)
{
    if (!s_display_running) {
        return 0;
    }
    return s_height / (FONT_GLYPH_HEIGHT + 1);
}
