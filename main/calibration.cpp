#include "calibration.h"

#include <atomic>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "app_config.h"
#include "led.h"
#include "soil_probe.h"

namespace calibration {

static const char *TAG = "calibration";

using namespace app_config::calibration;

static std::atomic<bool> s_active{false};

/* Written by start() only while no run is active; read by the run it starts. */
static saved_cb_t s_on_saved = nullptr;

/* The insert window is the time spent flashing the prompt, so the flash count
 * is derived from it rather than configured separately. */
static constexpr int kInsertFlashCount =
    kInsertWindowMs / (2 * kInsertFlashHalfPeriodMs);
static_assert(kInsertFlashCount > 0, "insert window shorter than one flash");

static void flash(led::color cl, int count, int half_period_ms)
{
    for (int i = 0; i < count; ++i) {
        led::set_color(cl);
        vTaskDelay(pdMS_TO_TICKS(half_period_ms));
        led::set_color(led::color::OFF);
        vTaskDelay(pdMS_TO_TICKS(half_period_ms));
    }
}

/* Flash to prompt the user, wait for the probe to settle, then sample. */
static esp_err_t capture(led::color prompt, const char *what, int *out_mv)
{
    ESP_LOGI(TAG, "insert the probe into %s soil within %d s",
             what, kInsertWindowMs / 1000);
    flash(prompt, kInsertFlashCount, kInsertFlashHalfPeriodMs);

    vTaskDelay(pdMS_TO_TICKS(kSettleMs));

    int sum = 0;
    for (int i = 0; i < kSampleCount; ++i) {
        if (i > 0) {
            vTaskDelay(pdMS_TO_TICKS(kSampleIntervalMs));
        }
        int mv = 0;
        esp_err_t err = soil_probe::read_raw_mv(&mv);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s reading failed: %s", what, esp_err_to_name(err));
            return err;
        }
        sum += mv;
    }

    *out_mv = (sum + kSampleCount / 2) / kSampleCount;
    ESP_LOGI(TAG, "%s reading: %d mV", what, *out_mv);
    return ESP_OK;
}

static void calibration_task(void *arg)
{
    ESP_LOGI(TAG, "calibration started");

    int dry_mv = 0;
    int wet_mv = 0;
    esp_err_t err = capture(led::color::RED, "dry", &dry_mv);
    if (err == ESP_OK) {
        err = capture(led::color::GREEN, "wet", &wet_mv);
    }
    if (err == ESP_OK) {
        /* Validates the pair, rejecting a swapped dry/wet order. */
        err = soil_probe::set_calibration(dry_mv, wet_mv);
    }

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "calibration succeeded");
        flash(led::color::GREEN, kResultFlashCount, kResultFlashHalfPeriodMs);

        /* After the flash, so the user's feedback is not held up by the
         * probe settle time inside the callback. */
        if (s_on_saved != nullptr) {
            s_on_saved();
        }
    } else {
        ESP_LOGW(TAG, "calibration failed; previous calibration kept");
        flash(led::color::RED, kResultFlashCount, kResultFlashHalfPeriodMs);
    }

    s_active = false;
    vTaskDelete(nullptr);
}

esp_err_t start(saved_cb_t on_saved)
{
    bool expected = false;
    if (!s_active.compare_exchange_strong(expected, true)) {
        ESP_LOGW(TAG, "calibration already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    /* Safe to set here: s_active guarantees no run is reading it. */
    s_on_saved = on_saved;

    if (xTaskCreate(calibration_task, "calibration", 3072, nullptr, 4, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "calibration task creation failed");
        s_active = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool active(void)
{
    return s_active;
}

}  // namespace calibration
