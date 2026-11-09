#ifndef ESP_WIFI_H
#define ESP_WIFI_H

#include "mock_esp.h"

typedef struct {
    int rssi;
} wifi_ap_record_t;

typedef enum {
    WIFI_MODE_NULL = 0,
    WIFI_MODE_STA = 1,
    WIFI_MODE_AP = 2,
    WIFI_MODE_APSTA = 3,
} wifi_mode_t;

typedef enum {
    WIFI_IF_STA = 0,
    WIFI_IF_AP = 1,
} wifi_interface_t;

esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap_info);
esp_err_t esp_wifi_get_mode(wifi_mode_t *mode);

#endif // ESP_WIFI_H