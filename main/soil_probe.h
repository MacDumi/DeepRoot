/*
 * Soil moisture probe — ADC reading and calibration.
 *
 * The probe needs a square-wave excitation to produce a reading at all, so
 * both read functions below drive it themselves: they start the waveform,
 * wait for the output filter to settle, sample, and stop it again. Callers
 * never have to think about it, and no reading is ever taken against an
 * undriven probe.
 *
 * The drive is gated rather than free-running to keep a primary cell alive —
 * see the excitation constants in soil_probe.cpp. The cost is that every read
 * blocks for the settling delay.
 */

#pragma once

#include <esp_adc/adc_oneshot.h>
#include <esp_err.h>
#include <esp_matter.h>
#include <stdint.h>

namespace soil_probe {

esp_err_t init(void);

/** The ADC1 oneshot handle, for other channels on the same unit.
 *
 * The ESP32-C6 has a single usable ADC unit and adc_oneshot_new_unit() claims
 * it exclusively, so a second caller cannot open its own. init() creates it
 * and everything else borrows it. NULL before init(). */
adc_oneshot_unit_handle_t adc_unit(void);

/** Take a moisture reading.
 *
 * @return 0-100 percent, or a null nullable if the reading is not
 *         trustworthy (probe disconnected, ADC out of calibrated range).
 */
nullable<uint8_t> read_percent(void);

/** Raw ADC millivolts, oversampled. Exposed for calibration runs.
 *
 * The returned value is the mean of several conversions, so repeated calls
 * should be stable to within a count or two — if they are not, the noise is
 * coming from outside the ADC. */
esp_err_t read_raw_mv(int *out_mv);

}  // namespace soil_probe
