#include "identify_indicator.h"

#include <atomic>
#include <cstddef>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

#include "app_config.h"
#include "calibration.h"
#include "led.h"

namespace identify_indicator {

static const char *TAG = "identify";

using namespace app_config::display;

static constexpr led::color kSequence[] = {
    led::color::RED,
    led::color::YELLOW,
    led::color::GREEN,
    led::color::OFF,
};
static constexpr size_t kSequenceLength = sizeof(kSequence) / sizeof(kSequence[0]);

static TimerHandle_t s_step_timer = nullptr;

/* Written by start()/stop() from any task. Everything else below runs on the
 * timer task only, which serialises it with itself and with
 * moisture_display's LED-off timer. */
static std::atomic<bool> s_active{false};
static size_t s_step = 0;
static TickType_t s_started_at = 0;

/* Timer task. Calibration may own the LED by now; leave it alone if so. */
static void finish(void)
{
    xTimerStop(s_step_timer, 0);
    if (!calibration::active()) {
        led::set_color(led::color::OFF);
    }
}

/* Timer task. */
static void step_cb(TimerHandle_t timer)
{
    /* Also covers a stop() whose end_cb could not be queued. */
    if (!s_active) {
        finish();
        return;
    }

    if (xTaskGetTickCount() - s_started_at >= pdMS_TO_TICKS(kIdentifyMaxMs)) {
        ESP_LOGW(TAG, "identify capped at %d s", kIdentifyMaxMs / 1000);
        s_active = false;
        finish();
        return;
    }

    const led::color cl = kSequence[s_step];
    s_step = (s_step + 1) % kSequenceLength;

    /* Suspended, not stopped: the sequence keeps its place and resumes on the
     * LED once the calibration run ends. */
    if (!calibration::active()) {
        led::set_color(cl);
    }
}

/* Timer task. */
static void begin_cb(void *arg, uint32_t unused)
{
    if (!s_active) {
        return;  // stopped again before this ran
    }
    s_step = 0;
    s_started_at = xTaskGetTickCount();
    step_cb(s_step_timer);  // first colour now, not one step late
    xTimerStart(s_step_timer, 0);
}

/* Timer task. */
static void end_cb(void *arg, uint32_t unused)
{
    finish();
}

esp_err_t init(void)
{
    s_step_timer = xTimerCreate("identify", pdMS_TO_TICKS(kIdentifyStepMs), pdTRUE, nullptr,
                                step_cb);
    return s_step_timer != nullptr ? ESP_OK : ESP_ERR_NO_MEM;
}

void start(void)
{
    if (s_step_timer == nullptr || s_active.exchange(true)) {
        return;
    }

    if (calibration::active()) {
        ESP_LOGI(TAG, "identify started; LED held by calibration until it ends");
    } else {
        ESP_LOGI(TAG, "identify started");
    }

    if (xTimerPendFunctionCall(begin_cb, nullptr, 0, 0) != pdPASS) {
        ESP_LOGW(TAG, "timer queue full; identify not shown");
        s_active = false;
    }
}

void stop(void)
{
    if (!s_active.exchange(false)) {
        return;  // not running, or already ended by the cap
    }
    ESP_LOGI(TAG, "identify stopped");

    /* If this cannot be queued, the next step_cb sees !s_active and finishes
     * instead, one step late. */
    xTimerPendFunctionCall(end_cb, nullptr, 0, 0);
}

bool active(void)
{
    return s_active;
}

}  // namespace identify_indicator
