#include "led.h"

#include <atomic>

#include <driver/gpio.h>
#include <esp_log.h>

#ifdef CONFIG_PM_ENABLE
#include <esp_pm.h>
#endif

#include "app_config.h"

namespace led {

static const char *TAG = "led";

using namespace app_config::led;

/* ---- Light-sleep interlock ------------------------------------------------ */
#ifdef CONFIG_PM_ENABLE
static esp_pm_lock_handle_t s_pm_lock = nullptr;
#endif

static std::atomic<bool> s_lit{false};

esp_err_t init() {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << kLedRedGpio | 1ULL << kLedGreenGpio | 1ULL << kLedYellowGpio;
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;

    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LED pins: %d", err);
        return err;
    }

#ifdef CONFIG_PM_ENABLE
    err = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "led", &s_pm_lock);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED PM lock: %d", err);
        return err;
    }
#endif

    return ESP_OK;
}

esp_err_t set_color(color cl) {

    uint8_t redLevel{0}, greenLevel{0}, yellowLevel{0};
    switch (cl) {
        case color::RED    : { redLevel = 1; break;};
        case color::YELLOW :{ yellowLevel = 1; break;};
        case color::GREEN  : { greenLevel = 1; break;};
        default : break;
    }

    const bool lit = cl != color::OFF;
    const bool was_lit = s_lit.exchange(lit);

#ifdef CONFIG_PM_ENABLE
    /* Before the pin goes high, so it never sits lit in a sleep. */
    if (lit && !was_lit && s_pm_lock != nullptr) {
        esp_pm_lock_acquire(s_pm_lock);
    }
#endif

    esp_err_t err = gpio_set_level(kLedRedGpio, redLevel);
    if (err == ESP_OK) {
        err = gpio_set_level(kLedGreenGpio, greenLevel);
    }

    if (err == ESP_OK) {
        err = gpio_set_level(kLedYellowGpio, yellowLevel);
    }

#ifdef CONFIG_PM_ENABLE
    /* After the pins are low; reached even if a level failed, so the lock is
     * never left held. */
    if (!lit && was_lit && s_pm_lock != nullptr) {
        esp_pm_lock_release(s_pm_lock);
    }
#endif

    return err;
}
} // namespace led