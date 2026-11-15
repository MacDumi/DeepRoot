#include "adc_bus.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "app_config.h"

namespace adc_bus {

static const char *TAG = "adc_bus";

/* Held from open() to close() */
static SemaphoreHandle_t s_lock = nullptr;
static adc_oneshot_unit_handle_t s_unit = nullptr;

esp_err_t init(void)
{
    s_lock = xSemaphoreCreateMutex();
    return s_lock != nullptr ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t open(adc_channel_t channel, adc_atten_t atten, adc_oneshot_unit_handle_t *out)
{
    if (s_lock == nullptr || out == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);

    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id = app_config::probe::kAdcUnit;
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_unit);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC unit create failed: %s", esp_err_to_name(err));
        s_unit = nullptr;
        xSemaphoreGive(s_lock);
        return err;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten = atten;
    chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    err = adc_oneshot_config_channel(s_unit, channel, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC channel %d config failed: %s", channel, esp_err_to_name(err));
        adc_oneshot_del_unit(s_unit);
        s_unit = nullptr;
        xSemaphoreGive(s_lock);
        return err;
    }

    *out = s_unit;
    return ESP_OK;
}

void close(void)
{
    if (s_unit != nullptr) {
        adc_oneshot_del_unit(s_unit);
        s_unit = nullptr;
    }
    xSemaphoreGive(s_lock);
}

}  // namespace adc_bus
