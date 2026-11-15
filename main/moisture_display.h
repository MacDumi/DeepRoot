/*
 * Shows a moisture reading on the LEDs.
 */

#pragma once

#include <esp_err.h>
#include <esp_matter.h>
#include <stdint.h>

namespace moisture_display {

/** Create the LED-off timer. After led::init(). */
esp_err_t init(void);

/** Light the LED for the given reading for kMoistureDisplayMs.
 *
 * Does nothing for a null reading, or while a calibration run owns the LEDs. */
void show(nullable<uint8_t> percent);

}  // namespace moisture_display
