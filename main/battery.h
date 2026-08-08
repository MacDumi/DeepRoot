/*
 * Battery gauge for AA alkaline cells — voltage via ADC, mapped to a
 * remaining-charge percentage.
 */

#pragma once

#include <esp_adc/adc_oneshot.h>
#include <esp_err.h>
#include <esp_matter.h>
#include <stdint.h>

namespace battery {

/** Configure the battery sense channel.
 *
 * @param adc  Shared ADC1 oneshot handle — see soil_probe::adc_unit(). The
 *             ESP32-C6 has one usable ADC unit and the oneshot driver permits
 *             a single handle per unit, so this must be borrowed, not created.
 */
esp_err_t init(adc_oneshot_unit_handle_t adc);

/** Estimate remaining charge.
 *
 * @return 0-100 percent, or null if the per-cell voltage is outside the
 *         range a connected alkaline cell could plausibly produce.
 */
nullable<uint8_t> read_percent(void);

/* Pack terminal voltage in millivolts */
esp_err_t read_millivolts(int *out_mv);

}  // namespace battery
