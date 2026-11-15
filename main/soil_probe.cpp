#include "soil_probe.h"

#include <cinttypes>

#include <driver/ledc.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <nvs.h>

#include "adc_bus.h"
#include "app_config.h"

#ifdef CONFIG_PM_ENABLE
#include <esp_pm.h>
#endif

namespace soil_probe {

static const char *TAG = "soil_probe";

using namespace app_config::probe;

/* ---- Probe excitation ----------------------------------------------------- */
static constexpr uint32_t kExcitationFullDuty = 1u << static_cast<uint32_t>(kExcitationBits);
static constexpr uint32_t kExcitationDuty =
    (kExcitationFullDuty * kExcitationDutyPercent + 50) / 100;

/* Calibration storage. Its own namespace, so a Matter factory reset (which
 * erases only esp_matter's namespaces) does not throw the calibration away. */
static constexpr char kNvsNamespace[] = "soil_cal";
static constexpr char kNvsDryKey[] = "dry_mv";
static constexpr char kNvsWetKey[] = "wet_mv";

static int s_dry_mv = kDefaultDryMv;
static int s_wet_mv = kDefaultWetMv;
static portMUX_TYPE s_cal_lock = portMUX_INITIALIZER_UNLOCKED;

static adc_cali_handle_t s_cali = nullptr;
static bool s_cali_enabled = false;

/* Mutex used by both the calibration and measurement tasks */
static SemaphoreHandle_t s_read_mutex = nullptr;

#ifdef CONFIG_PM_ENABLE
static esp_pm_lock_handle_t s_pm_lock = nullptr;
#endif

static void excitation_pm_acquire(void)
{
#ifdef CONFIG_PM_ENABLE
    if (s_pm_lock != nullptr) {
        esp_pm_lock_acquire(s_pm_lock);
    }
#endif
}

static void excitation_pm_release(void)
{
#ifdef CONFIG_PM_ENABLE
    if (s_pm_lock != nullptr) {
        esp_pm_lock_release(s_pm_lock);
    }
#endif
}

/* Configures the timer and channel but leaves the output idle low */
static esp_err_t excitation_init(void)
{
    ledc_timer_config_t timer_cfg = {};
    timer_cfg.speed_mode = kExcitationMode;
    timer_cfg.duty_resolution = kExcitationBits;
    timer_cfg.timer_num = kExcitationTimer;
    timer_cfg.freq_hz = kExcitationHz;
    /* Explicitly PLL_F80M rather than LEDC_AUTO_CLK: auto-selection is free to
     * pick RC_FAST, which is an uncalibrated internal oscillator. A drifting
     * excitation frequency shifts the probe's output and would show up as
     * slow, unexplained moisture drift. At 40 MHz it is also the only source
     * fast enough, so a mis-selection fails the config. */
    timer_cfg.clk_cfg = LEDC_USE_PLL_DIV_CLK;
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "excitation timer config failed: %s", esp_err_to_name(err));
        return err;
    }

    ledc_channel_config_t chan_cfg = {};
    chan_cfg.gpio_num = kExcitationPin;
    chan_cfg.speed_mode = kExcitationMode;
    chan_cfg.channel = kExcitationChannel;
    chan_cfg.timer_sel = kExcitationTimer;
    chan_cfg.intr_type = LEDC_INTR_DISABLE;
    chan_cfg.duty = 0;
    chan_cfg.hpoint = 0;
    /* Not the default NO_ALIVE_NO_PD. */
    chan_cfg.sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_ALLOW_PD;
    err = ledc_channel_config(&chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "excitation channel config failed: %s", esp_err_to_name(err));
        return err;
    }

    return ledc_stop(kExcitationMode, kExcitationChannel, 0);
}

static esp_err_t excitation_on(void)
{
    esp_err_t err = ledc_set_duty(kExcitationMode, kExcitationChannel, kExcitationDuty);
    if (err != ESP_OK) {
        return err;
    }
    return ledc_update_duty(kExcitationMode, kExcitationChannel);
}

static void excitation_off(void)
{
    ledc_stop(kExcitationMode, kExcitationChannel, 0);
}

static bool calibration_valid(int dry_mv, int wet_mv)
{
    return dry_mv - wet_mv >= kMinCalibrationSpanMv;
}

static void calibration_load(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        /* NOT_FOUND just means it has never been calibrated. */
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "calibration NVS open failed: %s", esp_err_to_name(err));
        }
        ESP_LOGW(TAG, "not calibrated; using defaults dry %d mV / wet %d mV",
                 kDefaultDryMv, kDefaultWetMv);
        return;
    }

    int32_t dry = 0, wet = 0;
    esp_err_t dry_err = nvs_get_i32(nvs, kNvsDryKey, &dry);
    esp_err_t wet_err = nvs_get_i32(nvs, kNvsWetKey, &wet);
    nvs_close(nvs);

    if (dry_err != ESP_OK || wet_err != ESP_OK || !calibration_valid(dry, wet)) {
        ESP_LOGW(TAG, "stored calibration missing or invalid; using defaults");
        return;
    }

    s_dry_mv = dry;
    s_wet_mv = wet;
    ESP_LOGI(TAG, "calibration loaded: dry %d mV / wet %d mV", s_dry_mv, s_wet_mv);
}

esp_err_t set_calibration(int dry_mv, int wet_mv)
{
    if (!calibration_valid(dry_mv, wet_mv)) {
        ESP_LOGW(TAG, "rejecting calibration dry %d mV / wet %d mV "
                      "(dry must exceed wet by %d mV)",
                 dry_mv, wet_mv, kMinCalibrationSpanMv);
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "calibration NVS open failed: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_set_i32(nvs, kNvsDryKey, dry_mv);
    if (err == ESP_OK) {
        err = nvs_set_i32(nvs, kNvsWetKey, wet_mv);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "calibration NVS write failed: %s", esp_err_to_name(err));
        return err;
    }

    taskENTER_CRITICAL(&s_cal_lock);
    s_dry_mv = dry_mv;
    s_wet_mv = wet_mv;
    taskEXIT_CRITICAL(&s_cal_lock);

    ESP_LOGI(TAG, "calibration saved: dry %d mV / wet %d mV", dry_mv, wet_mv);
    return ESP_OK;
}

esp_err_t init(void)
{
    calibration_load();

    s_read_mutex = xSemaphoreCreateMutex();
    if (s_read_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    adc_cali_curve_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id = kAdcUnit;
    cali_cfg.chan = kProbeChannel;
    cali_cfg.atten = kAttenuation;
    cali_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) == ESP_OK) {
        s_cali_enabled = true;
    } else {
        ESP_LOGW(TAG, "ADC calibration unavailable; readings will be raw counts");
    }

    esp_err_t err = excitation_init();
    if (err != ESP_OK) {
        return err;
    }

#ifdef CONFIG_PM_ENABLE
    err = esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, "soil_excite", &s_pm_lock);
    if (err != ESP_OK) {
        /* Fail loudly rather than silently taking readings through light sleep:
         * those would look plausible and be wrong. */
        ESP_LOGE(TAG, "could not create excitation PM lock: %s", esp_err_to_name(err));
        return err;
    }
#endif
    ESP_LOGI(TAG, "probe initialised on ADC%d channel %d, %" PRIu32
                  " Hz / 50%% excitation on GPIO%d",
             kAdcUnit + 1, kProbeChannel, kExcitationHz, kExcitationPin);
    return ESP_OK;
}

/* Body of read_raw_mv(); the caller holds s_read_mutex. */
static esp_err_t read_raw_mv_locked(int *out_mv)
{
    esp_err_t err;
    /* Held across the whole excitation window */
    excitation_pm_acquire();

    err = excitation_on();
    if (err != ESP_OK) {
        excitation_pm_release();
        ESP_LOGE(TAG, "excitation failed to start: %s", esp_err_to_name(err));
        return err;
    }
    vTaskDelay(kExcitationSettle);

    adc_oneshot_unit_handle_t adc = nullptr;
    err = adc_bus::open(kProbeChannel, kAttenuation, &adc);
    if (err != ESP_OK) {
        excitation_off();
        excitation_pm_release();
        return err;
    }

    /* Accumulate raw counts and convert once at the end. */
    uint32_t sum = 0;
    for (int i = 0; i < kOversampleCount; ++i) {
        /* Between samples only -- a trailing delay would add nothing but would
         * push the burst past a full period and unbalance the phases. */
        if (i > 0) {
            esp_rom_delay_us(kSampleIntervalUs);
        }

        int raw = 0;
        err = adc_oneshot_read(adc, kProbeChannel, &raw);
        if (err != ESP_OK) {
            break;
        }
        sum += static_cast<uint32_t>(raw);
    }

    adc_bus::close();
    excitation_off();
    excitation_pm_release();

    if (err != ESP_OK) {
        return err;
    }

    const int raw_mean =
        static_cast<int>((sum + kOversampleCount / 2) / kOversampleCount);

    if (s_cali_enabled) {
        return adc_cali_raw_to_voltage(s_cali, raw_mean, out_mv);
    }
    *out_mv = raw_mean;
    return ESP_OK;
}

esp_err_t read_raw_mv(int *out_mv)
{
    if (s_read_mutex == nullptr || out_mv == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_read_mutex, portMAX_DELAY);
    esp_err_t err = read_raw_mv_locked(out_mv);
    xSemaphoreGive(s_read_mutex);
    return err;
}

nullable<uint8_t> read_percent(void)
{
    nullable<uint8_t> unknown;

    int mv = 0;
    if (read_raw_mv(&mv) != ESP_OK) {
        ESP_LOGW(TAG, "ADC read failed");
        return unknown;
    }

    taskENTER_CRITICAL(&s_cal_lock);
    const int dry_mv = s_dry_mv;
    const int wet_mv = s_wet_mv;
    taskEXIT_CRITICAL(&s_cal_lock);

    /* Reject readings outside the plausible span -- most often a probe that
     * is not connected. Better to report "unknown" than a confident lie. */
    if (mv > dry_mv + kPlausibilityMarginMv || mv < wet_mv - kPlausibilityMarginMv) {
        ESP_LOGW(TAG, "reading %d mV outside calibrated span, reporting null", mv);
        return unknown;
    }

    if (mv > dry_mv) mv = dry_mv;
    if (mv < wet_mv) mv = wet_mv;

    /* Inverted: high mV = dry = low percent. */
    const int span = dry_mv - wet_mv;
    const int percent = (dry_mv - mv) * 100 / span;

    ESP_LOGD(TAG, "%d mV -> %d%%", mv, percent);
    return nullable<uint8_t>(static_cast<uint8_t>(percent));
}

}  // namespace soil_probe
