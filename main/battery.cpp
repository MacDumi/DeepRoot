#include "battery.h"

#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_log.h>

namespace battery {

static const char *TAG = "battery";

static constexpr adc_channel_t kBatteryChannel = ADC_CHANNEL_0;  // A0_D0
static constexpr adc_atten_t kAttenuation = ADC_ATTEN_DB_12;     // ~0-3.1 V

/* Plausibility window per cell, in millivolts.
 *
 * Below 800 mV nothing is connected — a real cell that far down is scrap and
 * the boost converter would have cut out long before.
 *
 * Note the lower bound sits below the 1000 mV "dead" point on purpose:
 * between 800 and 1000 mV the cell is present but flat, and 0% is the useful
 * answer there, not null. */
static constexpr int kMinPlausibleMv = 800;
static constexpr int kMaxPlausibleMv = 1800;

/* Light-load resting discharge curve for one AA alkaline cell, descending.
 *
 * Alkaline is far friendlier to voltage-based gauging than lithium: it has
 * no plateau, so the terminal voltage falls steadily from ~1.6 V fresh to
 * the conventional 1.0 V end-of-life and each voltage maps to a reasonably
 * distinct state of charge. The curve is still not a straight line — the
 * first tenth of a volt goes quickly and the last stretch below 1.2 V drops
 * away — hence the interpolated table rather than a linear map.
 */
struct CurvePoint {
    int mv;
    uint8_t percent;
};

static constexpr CurvePoint kCurve[] = {
    {1600, 100}, {1530, 90}, {1480, 80}, {1440, 70}, {1400, 60},
    {1360, 50},  {1320, 40}, {1280, 30}, {1240, 20}, {1190, 10},
    {1130, 5},   {1000, 0},
};

static constexpr size_t kCurvePoints = sizeof(kCurve) / sizeof(kCurve[0]);

static adc_oneshot_unit_handle_t s_adc = nullptr;
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

esp_err_t init(adc_oneshot_unit_handle_t adc)
{
    if (adc == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    s_adc = adc;

    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten = kAttenuation;
    chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, kBatteryChannel, &chan_cfg));

    adc_cali_curve_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id = ADC_UNIT_1;
    cali_cfg.chan = kBatteryChannel;
    cali_cfg.atten = kAttenuation;
    cali_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) == ESP_OK) {
        s_cali_enabled = true;
    } else {
        /* Without calibration the raw counts are not millivolts, so every
         * reading will fail the plausibility check and report null. That is
         * the intended failure: null beats a fabricated level. */
        ESP_LOGW(TAG, "ADC calibration unavailable; battery will report unknown");
    }

    ESP_LOGI(TAG, "battery sense on ADC channel %d", kBatteryChannel);
    return ESP_OK;
}

esp_err_t read_millivolts(int *out_mv)
{
    if (s_adc == nullptr || out_mv == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_cali_enabled) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    int raw = 0;
    esp_err_t err = adc_oneshot_read(s_adc, kBatteryChannel, &raw);
    if (err != ESP_OK) {
        return err;
    }

    err = adc_cali_raw_to_voltage(s_cali, raw, out_mv);
    if (err != ESP_OK) {
        return err;
    }

    return ESP_OK;
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
