/*
 * Shows Matter Identify on the LEDs.
 *
 * The pattern cycles red -> yellow -> green -> off.
 */

#pragma once

#include <esp_err.h>

namespace identify_indicator {

/** Create the step timer. After led::init(). */
esp_err_t init(void);

/** Start the pattern. Safe from any task; the LED is driven from the timer
 * task. Ignored if already running. */
void start(void);

/** Stop the pattern and turn the LED off, unless calibration owns it. Safe from
 * any task. */
void stop(void);

/** True while identifying. False again once the kIdentifyMaxMs cap is hit. */
bool active(void);

}  // namespace identify_indicator
