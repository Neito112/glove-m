#include "peripheral_output.h"
#include "config.h"
#include "gpio_policy.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

#include <string.h>
static const char *TAG = "periph_output";

int peripheral_analog_max(void)
{
#if CONFIG_IDF_TARGET_ESP32
    return 4095;
#else
    return 4095;
#endif
}

esp_err_t peripheral_analog_read(const peripheral_config_t *config,
                                 int *raw_out,
                                 int *millivolts_out)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    adc_unit_t unit = ADC_UNIT_1;
    adc_channel_t channel = ADC_CHANNEL_0;

    esp_err_t err = adc_oneshot_io_to_channel(config->pin_a, &unit, &channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO %d cannot be used as an ADC pin", config->pin_a);
        return err;
    }

    adc_oneshot_unit_handle_t unit_handle = NULL;
    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = unit,
        .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    err = adc_oneshot_new_unit(&unit_config, &unit_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create the ADC unit");
        return err;
    }

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    err = adc_oneshot_config_channel(unit_handle, channel, &channel_config);
    if (err != ESP_OK) {
        adc_oneshot_del_unit(unit_handle);
        ESP_LOGE(TAG, "Failed to configure the ADC channel");
        return err;
    }

    int raw = 0;
    err = adc_oneshot_read(unit_handle, channel, &raw);
    if (err != ESP_OK) {
        adc_oneshot_del_unit(unit_handle);
        ESP_LOGE(TAG, "Failed to read the ADC channel");
        return err;
    }

    // The calibration makes the voltage usable, but it is optional. The
    // curve fitting scheme is not available on every target, so the line
    // fitting scheme is tried first and the raw value is scaled otherwise.
    int millivolts = 0;
    adc_cali_handle_t cali_handle = NULL;
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .chan = channel,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle) == ESP_OK) {
        adc_cali_raw_to_voltage(cali_handle, raw, &millivolts);
        adc_cali_delete_scheme_curve_fitting(cali_handle);
    } else {
        millivolts = (raw * 3300) / peripheral_analog_max();
    }
#else
    (void)cali_config;
    millivolts = (raw * 3300) / peripheral_analog_max();
#endif

    adc_oneshot_del_unit(unit_handle);

    if (raw_out) {
        *raw_out = raw;
    }
    if (millivolts_out) {
        *millivolts_out = millivolts;
    }
    return ESP_OK;
}
