#include "soil_probe.h"

#include <cinttypes>

#include <driver/ledc.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace soil_probe {

static const char *TAG = "soil_probe";

static constexpr adc_unit_t kAdcUnit = ADC_UNIT_1;
static constexpr adc_channel_t kProbeChannel = ADC_CHANNEL_1;  // A1_D1
static constexpr adc_atten_t kAttenuation = ADC_ATTEN_DB_12;   // ~0-3.1 V

/* ---- Probe excitation -----------------------------------------------------
 *
 * The probe is driven by a square wave rather than DC: it measures the
 * capacitance of the soil dielectric, so it needs an AC excitation whose
 * rectified amplitude the ADC then reads. No signal means no reading, so the
 * output is meaningless unless the drive is running.
 *
 * D3 is GPIO21 on the XIAO ESP32-C6 (see the Seeed variant table). It is one
 * of the pins with no ADC channel behind it, so nothing is lost by using it
 * for a digital output.
 *
 * 8-bit resolution is the ceiling here, not a choice. The C6's fastest LEDC
 * source is PLL_F80M, and duty resolution is bounded by
 * log2(src_clk / freq) = log2(80 MHz / 200 kHz) = log2(400) ≈ 8.6, so 9 bits
 * would need a clock divider below 1. That puts the duty step at 1/256, and
 * 68% lands on 174 (67.97%) — the closest representable value. */
static constexpr gpio_num_t kExcitationPin = GPIO_NUM_21;  // D3
static constexpr ledc_mode_t kExcitationMode = LEDC_LOW_SPEED_MODE;  // C6 has no high-speed mode
static constexpr ledc_timer_t kExcitationTimer = LEDC_TIMER_0;
static constexpr ledc_channel_t kExcitationChannel = LEDC_CHANNEL_0;
static constexpr uint32_t kExcitationHz = 200000;
static constexpr ledc_timer_bit_t kExcitationBits = LEDC_TIMER_8_BIT;
static constexpr uint32_t kExcitationDutyPercent = 68;
static constexpr uint32_t kExcitationFullDuty = 1u << static_cast<uint32_t>(kExcitationBits);
static constexpr uint32_t kExcitationDuty =
    (kExcitationFullDuty * kExcitationDutyPercent + 50) / 100;

/* The drive is gated rather than left running: this samples once a minute on
 * a primary cell, so a permanently oscillating output and a permanently
 * powered probe would dominate the energy budget.
 *
 * That buys a settling delay. The probe's output is a rectified, RC-filtered
 * DC level, and reading before it has settled reports a wet-looking value
 * whatever the soil is doing — a failure that calibrates away invisibly and
 * then reappears in the field.
 *
 * TODO 50 ms is a guess and needs to be verified
 */
static constexpr TickType_t kExcitationSettle = pdMS_TO_TICKS(50);

static constexpr int kOversampleCount = 10;
static constexpr uint32_t kSampleIntervalUs = 200;

/* Calibration endpoints, in millivolts.
 *
 * Capacitive probes read HIGH when dry and LOW when wet — the mapping is
 * inverted relative to intuition. Obtain these with read_raw_mv():
 *   kDryMv : probe clean, dry, in air
 *   kWetMv : probe submerged in water to its marked depth
 *
 * These are placeholders
 */
static constexpr int kDryMv = 2600;  // TODO calibrate
static constexpr int kWetMv = 1100;  // TODO calibrate

/* Readings this far outside the calibrated span are treated as faults
 * rather than clamped, so a disconnected probe reports null instead of
 * a confident 0% or 100%. */
static constexpr int kPlausibilityMarginMv = 400;

static adc_oneshot_unit_handle_t s_adc = nullptr;
static adc_cali_handle_t s_cali = nullptr;
static bool s_cali_enabled = false;

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
     * slow, unexplained moisture drift. */
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

esp_err_t init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id = kAdcUnit;
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc));

    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten = kAttenuation;
    chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, kProbeChannel, &chan_cfg));

    /* Curve-fitting calibration is what makes readings comparable between boards. */
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

    ESP_LOGI(TAG, "probe initialised on ADC%d channel %d, %" PRIu32 " Hz / %" PRIu32
                  "%% excitation on GPIO%d",
             kAdcUnit + 1, kProbeChannel, kExcitationHz, kExcitationDutyPercent,
             kExcitationPin);
    return ESP_OK;
}

adc_oneshot_unit_handle_t adc_unit(void)
{
    return s_adc;
}

esp_err_t read_raw_mv(int *out_mv)
{
    if (s_adc == nullptr || out_mv == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = excitation_on();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "excitation failed to start: %s", esp_err_to_name(err));
        return err;
    }
    vTaskDelay(kExcitationSettle);

    /* Accumulate raw counts and convert once at the end. */
    uint32_t sum = 0;
    for (int i = 0; i < kOversampleCount; ++i) {
        /* Between samples only — a trailing delay would add nothing but would
         * push the burst past a full period and unbalance the phases. */
        if (i > 0) {
            esp_rom_delay_us(kSampleIntervalUs);
        }

        int raw = 0;
        err = adc_oneshot_read(s_adc, kProbeChannel, &raw);
        if (err != ESP_OK) {
            break;
        }
        sum += static_cast<uint32_t>(raw);
    }

    excitation_off();

    /* A failed sample aborts the whole reading rather than averaging what
     * arrived — a short burst is a different measurement, not a noisier one.
     */
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

nullable<uint8_t> read_percent(void)
{
    nullable<uint8_t> unknown;

    int mv = 0;
    if (read_raw_mv(&mv) != ESP_OK) {
        ESP_LOGW(TAG, "ADC read failed");
        return unknown;
    }

    /* Reject readings outside the plausible span — most often a probe that
     * is not connected. Better to report "unknown" than a confident lie. */
    if (mv > kDryMv + kPlausibilityMarginMv || mv < kWetMv - kPlausibilityMarginMv) {
        ESP_LOGW(TAG, "reading %d mV outside calibrated span, reporting null", mv);
        return unknown;
    }

    if (mv > kDryMv) mv = kDryMv;
    if (mv < kWetMv) mv = kWetMv;

    /* Inverted: high mV = dry = low percent. */
    const int span = kDryMv - kWetMv;
    const int percent = (kDryMv - mv) * 100 / span;

    ESP_LOGD(TAG, "%d mV -> %d%%", mv, percent);
    return nullable<uint8_t>(static_cast<uint8_t>(percent));
}

}  // namespace soil_probe
