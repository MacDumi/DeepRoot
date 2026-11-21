/*
 * On-device probe calibration, driven by the user button and the LEDs.
 *
 *   1. Red LED flashes for 10 s: insert the probe into completely dry soil.
 *   2. Red stops; after 3 s of settling the dry reading is taken.
 *   3. Green LED flashes for 10 s: insert the probe into fully wet soil.
 *   4. Green stops; after 3 s of settling the wet reading is taken.
 *   5. Two quick green flashes: saved. Two quick red flashes: rejected
 *      (usually the dry and wet steps were swapped) and the previous
 *      calibration is kept.
 */

#pragma once

#include <esp_err.h>

namespace calibration {

/** Start a calibration run in its own task and return immediately.
 *
 * @return ESP_ERR_INVALID_STATE if a run is already in progress.
 */
esp_err_t start(void);

/** True while a calibration run owns the LEDs. */
bool active(void);

}  // namespace calibration
