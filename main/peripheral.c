#include "peripheral.h"
#include "config.h"
#include "memory.h"
#include "nvs_keys.h"
#include "peripheral_audio.h"
#include "peripheral_display.h"
#include "peripheral_input.h"
#include "peripheral_output.h"

#include "cJSON.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "peripheral";

// The configuration of every peripheral type is stored as one JSON document
// under a single NVS key. That keeps the NVS namespace small and lets the
// whole setup be written in one go.
#define NVS_KEY_PERIPHERAL_CFG "periph_cfg"

static bool s_running[PERIPHERAL_TYPE_COUNT] = {false};

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

    // A few aliases so the agent can use the words a user would say.
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
    if (strcmp(name, "buzzer") == 0 || strcmp(name, "speaker") == 0) {
        *type_out = PERIPHERAL_SPEAKER;
        return true;
    }
    if (strcmp(name, "mic") == 0 || strcmp(name, "microphone") == 0) {
        *type_out = PERIPHERAL_MIC;
        return true;
    }

    return false;
}

// -----------------------------------------------------------------------------
// Serialization
// -----------------------------------------------------------------------------

static void config_to_json(const peripheral_config_t *config, cJSON *root)
{
    cJSON_AddNumberToObject(root, "pin_a", config->pin_a);
    cJSON_AddNumberToObject(root, "pin_b", config->pin_b);
    cJSON_AddNumberToObject(root, "pin_sw", config->pin_sw);
    cJSON_AddNumberToObject(root, "pin_sda", config->pin_sda);
    cJSON_AddNumberToObject(root, "pin_scl", config->pin_scl);
    cJSON_AddNumberToObject(root, "pin_miso", config->pin_miso);
    cJSON_AddNumberToObject(root, "pin_mosi", config->pin_mosi);
    cJSON_AddNumberToObject(root, "pin_dc", config->pin_dc);
    cJSON_AddNumberToObject(root, "pin_cs", config->pin_cs);
    cJSON_AddNumberToObject(root, "pin_clk", config->pin_clk);
    cJSON_AddNumberToObject(root, "pin_rst", config->pin_rst);
    cJSON_AddNumberToObject(root, "pin_ws", config->pin_ws);
    cJSON_AddNumberToObject(root, "pin_din", config->pin_din);
    cJSON_AddNumberToObject(root, "pin_dout", config->pin_dout);
    cJSON_AddNumberToObject(root, "pin_amp_bclk", config->pin_amp_bclk);
    cJSON_AddNumberToObject(root, "pin_amp_lr", config->pin_amp_lr);
    cJSON_AddNumberToObject(root, "pin_amp_din", config->pin_amp_din);
    cJSON_AddNumberToObject(root, "address", config->address);
    cJSON_AddNumberToObject(root, "frequency_hz", config->frequency_hz);
    cJSON_AddNumberToObject(root, "width", config->width);
    cJSON_AddNumberToObject(root, "height", config->height);
    cJSON_AddBoolToObject(root, "active_low", config->active_low);
    cJSON_AddBoolToObject(root, "pullup", config->pullup);
    cJSON_AddBoolToObject(root, "inverted", config->inverted);
}

static void config_from_json(const cJSON *root, peripheral_config_t *config)
{
    cJSON *item;

#define READ_NUMBER(field)                                  \
    item = cJSON_GetObjectItem(root, #field);               \
    if (item && cJSON_IsNumber(item)) {                     \
        config->field = item->valueint;                     \
    }

#define READ_BOOL(field)                                    \
    item = cJSON_GetObjectItem(root, #field);               \
    if (item && cJSON_IsBool(item)) {                      \
        config->field = cJSON_IsTrue(item);                 \
    }

    READ_NUMBER(pin_a)
    READ_NUMBER(pin_b)
    READ_NUMBER(pin_sw)
    READ_NUMBER(pin_sda)
    READ_NUMBER(pin_scl)
    READ_NUMBER(pin_miso)
    READ_NUMBER(pin_mosi)
    READ_NUMBER(pin_dc)
    READ_NUMBER(pin_cs)
    READ_NUMBER(pin_clk)
    READ_NUMBER(pin_rst)
    READ_NUMBER(pin_ws)
    READ_NUMBER(pin_din)
    READ_NUMBER(pin_dout)
    READ_NUMBER(pin_amp_bclk)
    READ_NUMBER(pin_amp_lr)
    READ_NUMBER(pin_amp_din)
    READ_NUMBER(address)
    READ_NUMBER(frequency_hz)
    READ_NUMBER(width)
    READ_NUMBER(height)
    READ_BOOL(active_low)
    READ_BOOL(pullup)
    READ_BOOL(inverted)

#undef READ_NUMBER
#undef READ_BOOL
}

// The stored document looks like this:
//
//   {"button":{"pin_a":5},"oled":{"pin_sda":4,"pin_scl":5}}
//
static bool load_document(cJSON **root_out)
{
    char raw[1024];

    if (!memory_get(NVS_KEY_PERIPHERAL_CFG, raw, sizeof(raw))) {
        return false;
    }

    cJSON *root = cJSON_Parse(raw);
    if (!root) {
        ESP_LOGW(TAG, "The stored peripheral configuration is broken");
        return false;
    }

    *root_out = root;
    return true;
}

bool peripheral_get_config(peripheral_type_t type, peripheral_config_t *config_out)
{
    if (type <= PERIPHERAL_NONE || type >= PERIPHERAL_TYPE_COUNT || !config_out) {
        return false;
    }

    cJSON *root = NULL;
    if (!load_document(&root)) {
        return false;
    }

    const char *name = peripheral_type_name(type);
    cJSON *entry = cJSON_GetObjectItem(root, name);    if (!entry || !cJSON_IsObject(entry)) {
        cJSON_Delete(root);
        return false;
    }

    memset(config_out, 0, sizeof(*config_out));
    // A pin that was never set stays unusable.
    config_out->pin_a = -1;
    config_out->pin_b = -1;
    config_out->pin_sw = -1;
    config_out->pin_sda = -1;
    config_out->pin_scl = -1;
    config_out->pin_miso = -1;
    config_out->pin_mosi = -1;
    config_out->pin_dc = -1;
    config_out->pin_cs = -1;
    config_out->pin_clk = -1;
    config_out->pin_rst = -1;
    config_out->pin_ws = -1;
    config_out->pin_din = -1;
    config_out->pin_dout = -1;
    config_out->pin_amp_bclk = -1;
    config_out->pin_amp_lr = -1;
    config_out->pin_amp_din = -1;
    config_out->pullup = true;

    config_from_json(entry, config_out);
    cJSON_Delete(root);
    return true;
}

esp_err_t peripheral_set_config(peripheral_type_t type, const peripheral_config_t *config)
{
    if (type <= PERIPHERAL_NONE || type >= PERIPHERAL_TYPE_COUNT || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = NULL;
    if (!load_document(&root)) {
        root = cJSON_CreateObject();
        if (!root) {
            return ESP_ERR_NO_MEM;
        }
    }

    const char *name = peripheral_type_name(type);
    cJSON *entry = cJSON_GetObjectItem(root, name);
    if (entry) {
        cJSON_DeleteItemFromObject(root, name);
    }

    entry = cJSON_CreateObject();
    if (!entry) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    config_to_json(config, entry);
    cJSON_AddItemToObject(root, name, entry);

    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = memory_set(NVS_KEY_PERIPHERAL_CFG, text);
    free(text);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Stored the configuration of the %s", name);
    }
    return err;
}

esp_err_t peripheral_remove_config(peripheral_type_t type)
{
    if (type <= PERIPHERAL_NONE || type >= PERIPHERAL_TYPE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = NULL;
    if (!load_document(&root)) {
        // Nothing stored, so there is nothing to remove either.
        return ESP_OK;
    }

    const char *name = peripheral_type_name(type);
    if (!cJSON_GetObjectItem(root, name)) {
        cJSON_Delete(root);
        return ESP_OK;
    }

    cJSON_DeleteItemFromObject(root, name);

    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = memory_set(NVS_KEY_PERIPHERAL_CFG, text);
    free(text);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Removed the configuration of the %s", name);
    }
    return err;
}

// -----------------------------------------------------------------------------
// Driver lifecycle
// -----------------------------------------------------------------------------

esp_err_t peripheral_start(peripheral_type_t type)
{
    peripheral_config_t config;

    if (!peripheral_get_config(type, &config)) {
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = ESP_OK;

    switch (type) {
        case PERIPHERAL_BUTTON:
            err = peripheral_button_start(&config);
            break;
        case PERIPHERAL_ENCODER:
            err = peripheral_encoder_start(&config);
            break;
        case PERIPHERAL_DISPLAY_OLED:
        case PERIPHERAL_DISPLAY_LCD:
            err = peripheral_display_start(&config);
            break;
        case PERIPHERAL_MIC:
            err = peripheral_mic_start(&config);
            break;
        case PERIPHERAL_SPEAKER:
            err = peripheral_speaker_start(&config);
            break;
        case PERIPHERAL_ANALOG:
            // The analog sensor has nothing to start, it is read on demand.
            err = ESP_OK;
            break;
        default:
            err = ESP_ERR_INVALID_ARG;
            break;
    }

    if (err == ESP_OK) {
        s_running[type] = true;
        ESP_LOGI(TAG, "Peripheral '%s' is running", peripheral_type_name(type));
    } else {
        ESP_LOGE(TAG, "Failed to start the %s: %s",
                 peripheral_type_name(type), esp_err_to_name(err));
    }

    return err;
}

void peripheral_stop(peripheral_type_t type)
{
    switch (type) {
        case PERIPHERAL_BUTTON:
            peripheral_button_stop();
            break;
        case PERIPHERAL_ENCODER:
            peripheral_encoder_stop();
            break;
        case PERIPHERAL_DISPLAY_OLED:
        case PERIPHERAL_DISPLAY_LCD:
            peripheral_display_stop();
            break;
        case PERIPHERAL_MIC:
            peripheral_mic_stop();
            break;
        case PERIPHERAL_SPEAKER:
            peripheral_speaker_stop();
            break;
        default:
            break;
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
    for (int i = 1; i < PERIPHERAL_TYPE_COUNT; ++i) {
        peripheral_type_t type = (peripheral_type_t)i;
        if (!peripheral_get_config(type, &(peripheral_config_t){0})) {
            continue;
        }
        peripheral_start(type);
    }
}

void peripheral_stop_all(void)
{
    for (int i = 1; i < PERIPHERAL_TYPE_COUNT; ++i) {
        peripheral_stop((peripheral_type_t)i);
    }
}

int peripheral_configured_count(void)
{
    int count = 0;
    for (int i = 1; i < PERIPHERAL_TYPE_COUNT; ++i) {
        peripheral_config_t config;
        if (peripheral_get_config((peripheral_type_t)i, &config)) {
            count++;
        }
    }
    return count;
}

void peripheral_report(char *buf, size_t buf_len)
{
    if (!buf || buf_len == 0) {
        return;
    }

    size_t offset = 0;
    int written = snprintf(buf, buf_len, "Configured peripherals:");
    if (written < 0) {
        buf[0] = '\0';
        return;
    }
    offset = (size_t)written < buf_len ? (size_t)written : buf_len - 1;

    for (int i = 1; i < PERIPHERAL_TYPE_COUNT; ++i) {
        peripheral_type_t type = (peripheral_type_t)i;
        peripheral_config_t config;

        if (!peripheral_get_config(type, &config)) {
            continue;
        }

        written = snprintf(buf + offset, buf_len - offset,
                            "\n  %s (%s) pin_a=%d pin_b=%d pin_sw=%d sda=%d scl=%d "
                            "mosi=%d clk=%d dc=%d cs=%d ws=%d din=%d dout=%d",
                            peripheral_type_name(type),
                            s_running[type] ? "running" : "stopped",
                            config.pin_a, config.pin_b, config.pin_sw,
                            config.pin_sda, config.pin_scl,
                            config.pin_mosi, config.pin_clk,
                            config.pin_dc, config.pin_cs,
                            config.pin_ws, config.pin_din, config.pin_dout);
        if (written < 0 || (size_t)written >= buf_len - offset) {
            break;
        }
        offset += (size_t)written;
    }
}
