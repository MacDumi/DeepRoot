#include "battery.h"

#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_log.h>
#include <esp_rom_sys.h>

#include "adc_bus.h"
#include "app_config.h"

namespace battery {

static const char *TAG = "battery";

using namespace app_config::battery;

static constexpr size_t kCurvePoints = sizeof(kCurve) / sizeof(kCurve[0]);

static adc_cali_handle_t s_cali = nullptr;
static bool s_cali_enabled = false;

/* Piecewise-linear interpolation across kCurve. */
static uint8_t curve_lookup(int mv)
{
    if (mv >= kCurve[0].mv) {
        return kCurve[0].percent;
    }
    if (mv <= kCurve[kCurvePoints - 1].mv) {
        return kCurve[kCurvePoints - 1].percent;
    }

    for (size_t i = 1; i < kCurvePoints; ++i) {
        if (mv >= kCurve[i].mv) {
            const CurvePoint &hi = kCurve[i - 1];
            const CurvePoint &lo = kCurve[i];
            const int span = hi.mv - lo.mv;
            const int delta = hi.percent - lo.percent;
            return static_cast<uint8_t>(lo.percent + (mv - lo.mv) * delta / span);
        }
    }
    return 0;
}

esp_err_t init(void)
{
    adc_cali_curve_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id = ADC_UNIT_1;
    cali_cfg.chan = kBatteryChannel;
    cali_cfg.atten = kAttenuation;
    cali_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) == ESP_OK) {
        s_cali_enabled = true;
    } else {
        /* Without calibration the raw counts are not millivolts */
        ESP_LOGW(TAG, "ADC calibration unavailable; battery will report unknown");
    }

    ESP_LOGI(TAG, "battery sense on ADC channel %d", kBatteryChannel);
    return ESP_OK;
}

esp_err_t read_millivolts(int *out_mv)
{
    if (out_mv == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_cali_enabled) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    adc_oneshot_unit_handle_t adc = nullptr;
    esp_err_t err = adc_bus::open(kBatteryChannel, kAttenuation, &adc);
    if (err != ESP_OK) {
        return err;
    }

    uint32_t sum = 0;
    for (int i = 0; i < kOversampleCount; ++i) {
        if (i > 0) {
            esp_rom_delay_us(kSampleIntervalUs);
        }

        int raw = 0;
        err = adc_oneshot_read(adc, kBatteryChannel, &raw);
        if (err != ESP_OK) {
            break;
        }
        sum += static_cast<uint32_t>(raw);
    }

    adc_bus::close();
    if (err != ESP_OK) {
        return err;
    }

    const int raw_mean =
        static_cast<int>((sum + kOversampleCount / 2) / kOversampleCount);

    return adc_cali_raw_to_voltage(s_cali, raw_mean, out_mv);
}

nullable<uint8_t> read_percent(void)
{
    nullable<uint8_t> unknown;

    int cell_mv = 0;
    if (read_millivolts(&cell_mv) != ESP_OK) {
        ESP_LOGW(TAG, "battery ADC read failed");
        return unknown;
    }

    if (cell_mv < kMinPlausibleMv || cell_mv > kMaxPlausibleMv) {
        ESP_LOGW(TAG, "%d mV/cell outside plausible alkaline range (%d-%d), "
                      "reporting null — check the sense pin",
                 cell_mv, kMinPlausibleMv, kMaxPlausibleMv);
        return unknown;
    }

    const uint8_t percent = curve_lookup(cell_mv);
    ESP_LOGI(TAG, "battery %d mV -> %u%%", cell_mv, percent);
    return nullable<uint8_t>(percent);
}

}  // namespace battery
