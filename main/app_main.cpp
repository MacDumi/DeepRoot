/*
 * Matter Soil Sensor - application entry point.
 *
 * Start-up order and the mapping from button gestures to actions. Everything
 * else lives in its own module:
 *
 *   antenna           RF switch, held through light sleep
 *   matter_node       data model + Matter/Thread/BLE start-up
 *   commissioning     keeps an unprovisioned node discoverable
 *   icd_support       ICD intervals, UAT, Check-In on subscription loss
 *   measurement       periodic moisture/battery reading and publishing
 *   button            press -> gesture recognition
 *   moisture_display  moisture shown on the LEDs
 */

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include <esp_matter.h>

#ifdef CONFIG_PM_ENABLE
#include <esp_pm.h>
#endif

#include "adc_bus.h"
#include "antenna.h"
#include "app_config.h"
#include "battery.h"
#include "button.h"
#include "calibration.h"
#include "icd_support.h"
#include "led.h"
#include "matter_node.h"
#include "measurement.h"
#include "moisture_display.h"
#include "soil_probe.h"

static const char *TAG = "app";

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

/* Must run before esp_matter::start(). A radio that initialises while light
 * sleep is still disabled decides it has no reason to sleep, takes an
 * ESP_PM_APB_FREQ_MAX lock and never releases it */
static void configure_power_management(void)
{
#ifdef CONFIG_PM_ENABLE
    esp_pm_config_t pm_config = {};
    pm_config.max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    pm_config.min_freq_mhz = app_config::power::kPmMinFreqMhz;
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
    pm_config.light_sleep_enable = true;
#endif
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));
#endif
}

/* Runs on the button task. */
static void on_button_gesture(button::gesture g)
{
    switch (g) {
    case button::gesture::SINGLE_PRESS:
        /* The reading costs a full probe excitation, so it goes to the
         * controller as well as to the LED. Skipped while a calibration run
         * owns the probe and the LEDs. */
        if (!calibration::active()) {
            moisture_display::show(measurement::update_moisture());
        }
        icd_support::notify_user_active();
        break;

    case button::gesture::MULTI_PRESS:
        ESP_LOGI(TAG, "probe calibration requested");
        calibration::start();
        break;

    case button::gesture::LONG_HOLD: {
        ESP_LOGW(TAG, "factory reset: erasing fabrics and network credentials");

        /* Erases the esp_matter NVS namespace, then schedules the CHIP-side
         * reset: deletes all fabrics, clears the chip-config namespaces and
         * reboots. The Thread operational dataset goes with the fabrics, so
         * the device comes back up unprovisioned and opens a commissioning
         * window. */
        esp_err_t err = esp_matter::factory_reset();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "factory_reset failed: %d", err);
        }
        break;
    }
    }
}

static void log_button_help(void)
{
    using namespace app_config::button;

    ESP_LOGI(TAG, "moisture display: press the user button once");
    ESP_LOGI(TAG, "probe calibration: press the user button %d times", kMultiPressCount);
    ESP_LOGI(TAG, "factory reset: hold the user button for %d s", kLongHoldMs / 1000);
#ifdef CONFIG_ENABLE_ICD_SERVER
    ESP_LOGI(TAG, "user active mode trigger: press the user button briefly");
#endif
}

extern "C" void app_main(void)
{
    /* First: the RF path has to be settled before anything brings a radio up. */
    ESP_ERROR_CHECK(antenna::init());

    init_nvs();
    configure_power_management();

    /* Before either ADC user: both open the unit through it per reading. */
    ESP_ERROR_CHECK(adc_bus::init());
    ESP_ERROR_CHECK(soil_probe::init());
    ESP_ERROR_CHECK(battery::init());

    ESP_ERROR_CHECK(led::init());
    ESP_ERROR_CHECK(moisture_display::init());

    /* First reading, taken here because no radio is up yet. */
    nullable<uint8_t> boot_moisture = soil_probe::read_percent();
    nullable<uint8_t> boot_battery = battery::read_percent();
    if (boot_moisture.is_null()) {
        ESP_LOGW(TAG, "boot moisture unknown");
    } else {
        ESP_LOGI(TAG, "boot moisture %u%%", boot_moisture.value());
    }

    ESP_ERROR_CHECK(matter_node::create(boot_moisture, boot_battery));
    ESP_ERROR_CHECK(matter_node::start());

    /* Failures below are logged rather than fatal. */
    measurement::start(matter_node::soil_endpoint_id(), boot_moisture, boot_battery);

    if (button::start(on_button_gesture) == ESP_OK) {
        log_button_help();
    } else {
        ESP_LOGE(TAG, "button unavailable - factory reset, calibration and the ICD "
                      "user active mode trigger are all disabled");
    }

    ESP_LOGI(TAG, "running; soil endpoint %u", matter_node::soil_endpoint_id());
}
