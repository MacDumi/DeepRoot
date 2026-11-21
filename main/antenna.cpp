#include "antenna.h"

#include <driver/gpio.h>
#include <esp_log.h>

#include "app_config.h"

namespace antenna {

static const char *TAG = "antenna";

using namespace app_config::antenna;

esp_err_t init(void)
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << kAntennaSelectGpio | 1ULL << kRfSwitchEnableGpio;
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;

    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_set_level(kAntennaSelectGpio, 1);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_set_level(kRfSwitchEnableGpio, 0);
    if (err != ESP_OK) {
        return err;
    }

#ifdef CONFIG_PM_ENABLE
    /* Leave both pins to IDF's automatic sleep switch, so they are released in
     * light sleep and driven again on wake. */
    err = gpio_sleep_sel_en(kAntennaSelectGpio);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_sleep_sel_en(kRfSwitchEnableGpio);
    if (err != ESP_OK) {
        return err;
    }
#endif

    ESP_LOGI(TAG, "external antenna selected");
    return ESP_OK;
}

}  // namespace antenna
