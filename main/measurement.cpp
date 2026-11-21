#include "measurement.h"

#include <algorithm>
#include <cinttypes>
#include <atomic>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "app_config.h"
#include "battery.h"
#include "soil_endpoint.h"
#include "soil_probe.h"

namespace measurement {

static const char *TAG = "measurement";

using namespace app_config::measure;

static constexpr uint32_t kTaskStack = 4096;
static constexpr UBaseType_t kTaskPriority = 5;

static uint16_t s_endpoint_id = 0;
/* Set once s_endpoint_id is valid; the button task may publish from then on. */
static std::atomic<bool> s_publishing{false};

static nullable<uint8_t> s_published_moisture;
static nullable<uint8_t> s_published_battery;
static SemaphoreHandle_t s_moisture_lock = nullptr;

/* A change to or from null is always significant: it is the probe or the
 * sense path failing, or recovering. */
static bool differs_by(nullable<uint8_t> published, nullable<uint8_t> reading, uint8_t band)
{
    if (published.is_null() || reading.is_null()) {
        return published.is_null() != reading.is_null();
    }
    const int delta = static_cast<int>(reading.value()) - static_cast<int>(published.value());
    return delta >= band || -delta >= band;
}

static bool battery_significant(nullable<uint8_t> published, nullable<uint8_t> reading)
{
    if (differs_by(published, reading, kBatteryDeadbandPercent)) {
        return true;
    }

    return !published.is_null() && !reading.is_null() &&
           soil_sensor::charge_level(published.value()) !=
               soil_sensor::charge_level(reading.value());
}

/* ---- Adaptive cadence ---------------------------------------------------- */
enum class tier {
    NORMAL,      // ordered by dryness: a greater tier measures more often
    ALMOST_DRY,
    DRY,
};

static tier s_tier = tier::ALMOST_DRY;
static TickType_t s_last_reading_at = 0;
static TaskHandle_t s_task = nullptr;

static tier classify(int percent)
{
    if (percent <= kDryPercent) {
        return tier::DRY;
    }
    if (percent <= kAlmostDryPercent) {
        return tier::ALMOST_DRY;
    }
    return tier::NORMAL;
}

/* Drier: move as soon as a threshold is crossed. Wetter: only once the reading
 * clears the threshold by kTierHysteresisPercent. */
static tier next_tier(tier current, nullable<uint8_t> reading)
{
    if (reading.is_null()) {
        return tier::ALMOST_DRY;
    }
    const int percent = reading.value();
    const tier now = classify(percent);
    if (now >= current) {
        return now;
    }
    return std::min(current, classify(percent - kTierHysteresisPercent));
}

static TickType_t interval_for(tier t)
{
    switch (t) {
    case tier::DRY:        return kIntervalDry;
    case tier::ALMOST_DRY: return kIntervalAlmostDry;
    case tier::NORMAL:     break;
    }
    return kIntervalNormal;
}

static const char *tier_name(tier t)
{
    switch (t) {
    case tier::DRY:        return "dry";
    case tier::ALMOST_DRY: return "almost dry";
    case tier::NORMAL:     break;
    }
    return "normal";
}

/* Caller holds s_moisture_lock. */
static void update_tier_locked(nullable<uint8_t> reading)
{
    s_last_reading_at = xTaskGetTickCount();
    const tier t = next_tier(s_tier, reading);
    if (t != s_tier) {
        ESP_LOGI(TAG, "measurement tier %s -> %s: next reading in %" PRIu32 " min",
                 tier_name(s_tier), tier_name(t),
                 static_cast<uint32_t>(interval_for(t) / configTICK_RATE_HZ / 60));
        s_tier = t;
    }
}

static nullable<uint8_t> measure_moisture(bool force)
{
    nullable<uint8_t> percent = soil_probe::read_percent();

    if (percent.is_null()) {
        ESP_LOGW(TAG, "moisture unknown");
    } else {
        ESP_LOGI(TAG, "moisture %u%%", percent.value());
    }

    if (!s_publishing) {
        return percent;
    }

    xSemaphoreTake(s_moisture_lock, portMAX_DELAY);
    update_tier_locked(percent);
    if (force || differs_by(s_published_moisture, percent, kMoistureDeadbandPercent)) {
        esp_err_t err = soil_sensor::report_moisture(s_endpoint_id, percent);
        if (err == ESP_OK) {
            s_published_moisture = percent;
        } else {
            ESP_LOGE(TAG, "report_moisture failed: %d", err);
        }
    } else {
        ESP_LOGD(TAG, "moisture within %u%% of published value; not reported",
                 kMoistureDeadbandPercent);
    }
    xSemaphoreGive(s_moisture_lock);

    return percent;
}

nullable<uint8_t> update_moisture(void)
{
    nullable<uint8_t> percent = measure_moisture(true);
    /* The reading restarted the clock and may have changed the tier; have the
     * measuring task recompute its wait. */
    if (s_task != nullptr) {
        xTaskNotifyGive(s_task);
    }
    return percent;
}

static void update_battery(void)
{
    /* read_percent() logs the cell millivolts */
    nullable<uint8_t> percent = battery::read_percent();

    if (percent.is_null()) {
        ESP_LOGW(TAG, "battery unknown");
    }

    if (!battery_significant(s_published_battery, percent)) {
        ESP_LOGD(TAG, "battery within %u%% of published value; not reported",
                 kBatteryDeadbandPercent);
        return;
    }

    esp_err_t err = soil_sensor::report_battery(s_endpoint_id, percent);
    if (err == ESP_OK) {
        s_published_battery = percent;
    } else {
        ESP_LOGE(TAG, "report_battery failed: %d", err);
    }
}

static void measure_task(void *arg)
{
    /* Elapsed-time gate rather than a loop counter, so the moisture interval
     * changing with the tier cannot silently rescale the battery cadence. */
    TickType_t last_battery = xTaskGetTickCount();

    for (;;) {
        /* Wait first: app_main() already measured before the radios came up
         * and seeded the attributes with it.
         *
         * The wait is the current tier's interval counted from the last
         * reading, whoever took it. */
        xSemaphoreTake(s_moisture_lock, portMAX_DELAY);
        const TickType_t interval = interval_for(s_tier);
        const TickType_t last = s_last_reading_at;
        xSemaphoreGive(s_moisture_lock);

        /* Tick wraparound is handled by the unsigned subtraction. */
        const TickType_t elapsed = xTaskGetTickCount() - last;
        if (elapsed < interval && ulTaskNotifyTake(pdTRUE, interval - elapsed) != 0) {
            continue;
        }

        measure_moisture(false);

        const TickType_t now = xTaskGetTickCount();
        if ((now - last_battery) >= kBatteryInterval) {
            last_battery = now;
            update_battery();
        }
    }
}

esp_err_t start(uint16_t endpoint_id, nullable<uint8_t> boot_moisture,
                nullable<uint8_t> boot_battery)
{
    s_moisture_lock = xSemaphoreCreateMutex();
    if (s_moisture_lock == nullptr) {
        ESP_LOGE(TAG, "moisture lock creation FAILED - nothing will be published");
        return ESP_ERR_NO_MEM;
    }

    s_endpoint_id = endpoint_id;
    s_published_moisture = boot_moisture;
    s_published_battery = boot_battery;
    s_tier = next_tier(tier::NORMAL, boot_moisture);
    s_last_reading_at = xTaskGetTickCount();
    s_publishing = true;
    ESP_LOGI(TAG, "measurement tier %s: next reading in %" PRIu32 " min", tier_name(s_tier),
             static_cast<uint32_t>(interval_for(s_tier) / configTICK_RATE_HZ / 60));

    if (xTaskCreate(measure_task, "measure", kTaskStack, nullptr, kTaskPriority, &s_task) !=
        pdPASS) {
        ESP_LOGE(TAG, "measure task creation FAILED - no further periodic measurements "
                      "will be taken; attributes stay at the boot reading");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

}  // namespace measurement
