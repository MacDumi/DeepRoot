/*
 * Application configuration: pin assignments, peripheral allocation, timing
 * and thresholds.
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <hal/adc_types.h>
#include <hal/gpio_types.h>
#include <hal/ledc_types.h>
#include <sdkconfig.h>

namespace app_config {

/* ---- Pin map (XIAO ESP32-C6) ----------------------------------------------
 *
 *   GPIO0  A0_D0   battery divider (ADC1_CH0)
 *   GPIO1  A1_D1   soil probe output (ADC1_CH1)
 *   GPIO2  A2_D2   user button
 *   GPIO3          RF switch enable
 *   GPIO14         antenna select
 *   GPIO18 A10_D10 yellow LED
 *   GPIO19 A8_D8   green LED
 *   GPIO20 A9_D9   red LED
 *   GPIO21 D3      probe excitation
 * ------------------------------------------------------------------------- */

namespace antenna {

/* The XIAO ESP32-C6 has an RF switch between the on-board antenna and the
 * u.FL connector. GPIO14 high selects the external one.
 */
inline constexpr gpio_num_t kAntennaSelectGpio = GPIO_NUM_14;
inline constexpr gpio_num_t kRfSwitchEnableGpio = GPIO_NUM_3;

}  // namespace antenna

namespace radio {

/* 802.15.4 transmit power, in dBm. */
inline constexpr int8_t kThreadTxPowerDbm = 8;

}  // namespace radio

namespace power {

/* DFS floor, in MHz. Must be the XTAL rate or an integer division of it
 * (CONFIG_XTAL_FREQ is 40 here, so this is XTAL/4); esp_pm_configure() rejects
 * anything below REF_CLK_FREQ * REF_CLK_DIV_MIN. */
inline constexpr int kPmMinFreqMhz = 10;

}  // namespace power

namespace measure {

/* Minutes to ticks. Not pdMS_TO_TICKS(): its ms * tick-rate product overflows
 * a 32-bit TickType_t past ~71 minutes at CONFIG_FREERTOS_HZ=1000. */
constexpr TickType_t minutes_to_ticks(uint32_t minutes)
{
    return static_cast<TickType_t>(minutes * 60) * configTICK_RATE_HZ;
}

/* Measurement cadence, adaptive on the last moisture reading.
 *
 * Moisture tiers:
 *
 *   moisture >  kAlmostDryPercent    normal      kIntervalNormal      8 h
 *   moisture <= kAlmostDryPercent    almost dry  kIntervalAlmostDry   2 h
 *   moisture <= kDryPercent          dry         kIntervalDry        30 min
 *   reading unknown (null)                       kIntervalAlmostDry
 *
 * Every reading, including a button press, picks the tier and restarts the
 * clock, so watering a dry pot and pressing the button moves it to the slow
 * tier at once.
 *
 * Moving to a faster (drier) tier happens as soon as a reading crosses its
 * threshold. Moving back to a slower one needs the reading to clear the
 * threshold by kTierHysteresisPercent.
 */
inline constexpr uint8_t kDryPercent = 25;
inline constexpr uint8_t kAlmostDryPercent = 40;
inline constexpr uint8_t kTierHysteresisPercent = 3;

#ifdef CONFIG_ENABLE_ICD_SERVER
inline constexpr TickType_t kIntervalNormal = minutes_to_ticks(8 * 60);
inline constexpr TickType_t kIntervalAlmostDry = minutes_to_ticks(2 * 60);
inline constexpr TickType_t kIntervalDry = minutes_to_ticks(30);
#else
inline constexpr TickType_t kIntervalNormal = minutes_to_ticks(1);
inline constexpr TickType_t kIntervalAlmostDry = minutes_to_ticks(1);
inline constexpr TickType_t kIntervalDry = minutes_to_ticks(1);
#endif

/* Battery cadence. Checked after each moisture reading, so in the normal tier
 * the effective interval is the 8 h measurement interval. */
inline constexpr TickType_t kBatteryInterval = minutes_to_ticks(8 * 60);

/* Reporting deadbands, in percentage points. */
inline constexpr uint8_t kMoistureDeadbandPercent = 3;
inline constexpr uint8_t kBatteryDeadbandPercent = 5;

}  // namespace measure

namespace icd {

/* ---------------------------------------------------------------------
 * ICD (Intermittently Connected Device), Long Idle Time profile.
 * ------------------------------------------------------------------- */

/* Radio wake rate in idle mode: how often the node polls its Thread parent for
 * buffered traffic. Must stay well under kThreadChildTimeoutSec below, or the
 * parent drops the device between polls.
 *
 * Only applies once a controller has registered as a LIT client. Until then
 * CHIP runs the node as SIT and caps this at 15 s
 * (ICDConfigurationData::GetSlowPollingInterval()).
 */
inline constexpr uint32_t kIcdSlowPollMs = 120 * 1000;

/* Thread child timeout: how long the parent keeps this child in its table
 * without hearing from it. The child picks the value and sends it to the parent
 * at attach. The OpenThread default (240 s) is only two slow polls, so one lost
 * poll plus jitter cost a full re-attach, which is far dearer than polling.
 *
 * Raising it does not change the poll rate. OpenThread polls at the shorter of
 * a timeout-derived period and the period CHIP sets (DataPollSender::
 * CalculatePollPeriod()). The cost is that the parent holds a slot, and buffers
 * frames, for a dead child that much longer. */
inline constexpr uint32_t kThreadChildTimeoutPolls = 5;
inline constexpr uint32_t kThreadChildTimeoutSec = kThreadChildTimeoutPolls * kIcdSlowPollMs / 1000;

static_assert(kThreadChildTimeoutPolls >= 3, "child timeout must tolerate at least two lost polls");
static_assert(kThreadChildTimeoutSec <= 8 * 60 * 60,
              "OpenThread clamps the child timeout to 8 h (Mle::SetTimeout())");

/* Poll rate once something is happening. Left at the CHIP default */
inline constexpr uint32_t kIcdFastPollMs = 200;

/* Check-in cadence for registered clients, and the ceiling a controller can
 * negotiate for its subscription MaxInterval. Spec allows up to 18 h. */
inline constexpr uint32_t kIcdIdleModeSec = 60 * 60;

/* How long active mode is held once entered. Needs to cover a report plus its
 * acknowledgement across a multi-hop mesh, not just one round trip.
 */
inline constexpr uint32_t kIcdActiveModeMs = 5 * 1000;

/* Extra active time granted after each message. 5000 ms is not a preference:
 * esp_matter's set_active_threshold() rejects anything lower once the mode is
 * LIT. */
inline constexpr uint32_t kIcdActiveThresholdMs = 5000;

/* Thread child supervision.
 *
 * The child asks its parent for this supervision interval: whenever the parent
 * has sent the child nothing for this long, it queues an empty keep-alive
 * frame. A sleepy child only collects that frame on its next poll, so it can
 * arrive up to interval + slow poll after the parent's last frame. Pinned to
 * the OpenThread default so the timeout below stays consistent with it. */
inline constexpr uint16_t kThreadSupervisionIntervalSec = 129;

/* If the child hears nothing from its parent for this long, it sends an MLE
 * Child Update Request to confirm the parent still holds it in its table. The
 * OpenThread default (190 s) is shorter than interval + slow poll (249 s), so
 * it fired on every cycle and added a request/response exchange every 190 s.
 * The margin covers poll jitter and one lost keep-alive. */
inline constexpr uint16_t kThreadSupervisionMarginSec = 30;
inline constexpr uint16_t kThreadSupervisionCheckTimeoutSec =
    kThreadSupervisionIntervalSec + kIcdSlowPollMs / 1000 + kThreadSupervisionMarginSec;

static_assert(kThreadSupervisionCheckTimeoutSec * 1000ULL >
                  kThreadSupervisionIntervalSec * 1000ULL + kIcdSlowPollMs,
              "supervision check timeout must exceed interval + slow poll");

}  // namespace icd

namespace button {

inline constexpr gpio_num_t kUserButtonGpio = GPIO_NUM_2;  // A2_D2
inline constexpr int kButtonPollMs = 20;

/* Long hold: factory reset. */
inline constexpr int kLongHoldMs = 5000;

/* Anything shorter than this is contact bounce, not a press. */
inline constexpr int kButtonDebounceMs = 50;

/* Multi-press: a "short" press is released before this, and consecutive
 * short presses belong to one gesture if the next starts within the gap. */
inline constexpr int kShortPressMaxMs = 1000;
inline constexpr int kMultiPressGapMs = 500;

/* Multi-press: probe calibration. */
inline constexpr int kMultiPressCount = 3;

}  // namespace button

namespace display {

/* A single press shows the current moisture on the LED for a moment, coloured
 * by the measurement tier (measure::kDryPercent, measure::kAlmostDryPercent):
 * red when dry, yellow when almost dry, green otherwise. */
inline constexpr int kMoistureDisplayMs = 1000;

/* Matter Identify: red -> yellow -> green -> off, one step each, so a full
 * cycle is 4 x kIdentifyStepMs.
 *
 * kIdentifyMaxMs caps how long the LED runs whatever IdentifyTime a controller
 * asks for (up to 65535 s): a lit LED blocks light sleep. */
inline constexpr int kIdentifyStepMs = 200;
inline constexpr int kIdentifyMaxMs = 30 * 1000;

}  // namespace display

namespace led {

inline constexpr gpio_num_t kLedRedGpio    = GPIO_NUM_20; // A9_D9
inline constexpr gpio_num_t kLedGreenGpio  = GPIO_NUM_19; // A8_D8
inline constexpr gpio_num_t kLedYellowGpio = GPIO_NUM_18; // A10_D10

}  // namespace led

namespace calibration {

/* Time the user has to move the probe, while the LED flashes. */
inline constexpr int kInsertWindowMs = 10000;
inline constexpr int kInsertFlashHalfPeriodMs = 250;

/* After the flashing stops: lets the probe and the soil around it settle
 * before the reading is taken. */
inline constexpr int kSettleMs = 3000;

/* Each read_raw_mv() is already oversampled; averaging a few spaced calls
 * on top also smooths out the probe being nudged. */
inline constexpr int kSampleCount = 5;
inline constexpr int kSampleIntervalMs = 100;

inline constexpr int kResultFlashCount = 2;
inline constexpr int kResultFlashHalfPeriodMs = 150;

}  // namespace calibration

namespace probe {

inline constexpr adc_unit_t kAdcUnit = ADC_UNIT_1;
inline constexpr adc_channel_t kProbeChannel = ADC_CHANNEL_1;  // A1_D1
inline constexpr adc_atten_t kAttenuation = ADC_ATTEN_DB_12;   // ~0-3.1 V

/* ---- Probe excitation ---------------------------------------------------- */
inline constexpr gpio_num_t kExcitationPin = GPIO_NUM_21;  // D3
inline constexpr ledc_mode_t kExcitationMode = LEDC_LOW_SPEED_MODE;  // C6 has no high-speed mode
inline constexpr ledc_timer_t kExcitationTimer = LEDC_TIMER_0;
inline constexpr ledc_channel_t kExcitationChannel = LEDC_CHANNEL_0;

inline constexpr uint32_t kExcitationHz = 500000;
inline constexpr ledc_timer_bit_t kExcitationBits = LEDC_TIMER_7_BIT;
inline constexpr uint32_t kExcitationDutyPercent = 50;

/* Settling time after the excitation starts. The probe's output is a
 * rectified, RC-filtered DC level, and reading before it has settled reports
 * a wet-looking value whatever the soil is doing.
 */
inline constexpr TickType_t kExcitationSettle = pdMS_TO_TICKS(800);

inline constexpr int kOversampleCount = 10;
inline constexpr uint32_t kSampleIntervalUs = 200;

/* Calibration endpoints, in millivolts.
 *
 * Capacitive probes read HIGH when dry and LOW when wet. The live pair is measured
 * on the device and stored in NVS; these defaults are only placeholders used until
 * that has been done once.
 */
inline constexpr int kDefaultDryMv = 2600;
inline constexpr int kDefaultWetMv = 1100;

/* A dry/wet pair closer together than this cannot resolve moisture usefully,
 * and is most often a calibration that read the same soil twice. */
inline constexpr int kMinCalibrationSpanMv = 200;

/* Readings this far outside the calibrated span are treated as faults
 * rather than clamped, so a disconnected probe reports null instead of
 * a confident 0% or 100%. */
inline constexpr int kPlausibilityMarginMv = 400;

/* Accuracy published in SoilMoistureMeasurementLimits. */
inline constexpr uint64_t kAccuracyFixedMaxPercent = 5;

}  // namespace probe

namespace battery {

inline constexpr adc_channel_t kBatteryChannel = ADC_CHANNEL_0;  // A0_D0
inline constexpr adc_atten_t kAttenuation = ADC_ATTEN_DB_6;      // ~0-2.2 V

/* Plausibility window per cell, in millivolts.
 *
 * Below 800 mV nothing is connected -- a real cell that far down is scrap and
 * the boost converter would have cut out long before. */
inline constexpr int kMinPlausibleMv = 800;
inline constexpr int kMaxPlausibleMv = 1800;

/* Discharge curve for one AA alkaline cell, descending.
 *
 * The shape is the typical alkaline closed-circuit curve at tens of mA,
 * rescaled so 100-0% spans fresh to 1.10 V.
 */
struct CurvePoint {
    int mv;
    uint8_t percent;
};

inline constexpr CurvePoint kCurve[] = {
    {1550, 100}, {1500, 97}, {1450, 91}, {1400, 83}, {1350, 71},
    {1300, 57},  {1250, 43}, {1200, 28}, {1150, 13}, {1100, 0}
};

inline constexpr int kOversampleCount = 10;
inline constexpr uint32_t kSampleIntervalUs = 200;

/* Thresholds for the coarse PowerSource attributes (BatChargeLevel and
 * BatReplacementNeeded) derived from the percentage. On kCurve, warning is
 * ~1.22 V and critical ~1.17 V under load. */
inline constexpr uint8_t kBatteryWarningPercent = 35;
inline constexpr uint8_t kBatteryCriticalPercent = 20;

}  // namespace battery

namespace ota {

/* Trial of a freshly installed OTA image.
 *
 * kHealthyAttachedSec: continuous Thread attachment needed to confirm it.
 * kTrialTimeoutSec: if that has not happened by then, roll back.
 * kCheckIntervalSec: how often to look. Only runs during the trial. */
inline constexpr uint32_t kHealthyAttachedSec = 60;
inline constexpr uint32_t kTrialTimeoutSec = 15 * 60;
inline constexpr uint32_t kCheckIntervalSec = 10;

static_assert(kHealthyAttachedSec < kTrialTimeoutSec, "trial must allow time to confirm");

}  // namespace ota

namespace factory {

/* Device identity for the Basic Information cluster. */
inline constexpr char kVendorName[]   = "DIY";
inline constexpr char kProductName[]  = "Soil Sensor";
inline constexpr char kHwVerString[]  = "XIAO ESP32-C6";
inline constexpr char kSerialPrefix[] = "SOIL-";

}  // namespace factory

}  // namespace app_config
