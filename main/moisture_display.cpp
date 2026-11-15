#include "moisture_display.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

#include "app_config.h"
#include "calibration.h"
#include "led.h"

namespace moisture_display {

static const char *TAG = "moisture_display";

using namespace app_config::display;

static TimerHandle_t s_led_off_timer = nullptr;

static void led_off_timer_cb(TimerHandle_t timer)
{
    /* A calibration started while the display was up owns the LEDs now. */
    if (!calibration::active()) {
        led::set_color(led::color::OFF);
    }
}

esp_err_t init(void)
{
    s_led_off_timer = xTimerCreate("led_off", pdMS_TO_TICKS(kMoistureDisplayMs), pdFALSE,
                                   nullptr, led_off_timer_cb);
    return s_led_off_timer != nullptr ? ESP_OK : ESP_ERR_NO_MEM;
}

void show(nullable<uint8_t> percent)
{
    if (calibration::active() || s_led_off_timer == nullptr) {
        return;
    }

    if (percent.is_null()) {
        ESP_LOGW(TAG, "moisture unknown; nothing to show");
        return;
    }

    led::color cl = led::color::GREEN;
    if (percent.value() < kMoistureDryPercent) {
        cl = led::color::RED;
    } else if (percent.value() < kMoistureWetPercent) {
        cl = led::color::YELLOW;
    }
    ESP_LOGI(TAG, "showing moisture %u%%", percent.value());

    led::set_color(cl);
    /* Starts the timer, or restarts it if a previous display is still up. */
    xTimerReset(s_led_off_timer, 0);
}

}  // namespace moisture_display
