/*
 * Battery gauge for AA alkaline cells -- voltage via ADC, mapped to a
 * remaining-charge percentage.
 */

#pragma once

#include <esp_adc/adc_oneshot.h>
#include <esp_err.h>
#include <esp_matter.h>
#include <stdint.h>

namespace battery {

/** Set up the battery sense calibration.
 *
 * The ADC unit itself is opened per reading through adc_bus, which the soil
 * probe shares. */
esp_err_t init(void);

/** Estimate remaining charge.
 *
 * @return 0-100 percent, or null if the per-cell voltage is outside the
 *         range a connected alkaline cell could plausibly produce.
 */
nullable<uint8_t> read_percent(void);

/* Pack terminal voltage in millivolts */
esp_err_t read_millivolts(int *out_mv);

}  // namespace battery
