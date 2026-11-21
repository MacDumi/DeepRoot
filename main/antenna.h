/*
 * Antenna select.
 *
 * The XIAO ESP32-C6 has an RF switch between the on-board antenna and the
 * u.FL connector. GPIO14 high selects the external one; GPIO3 low powers the
 * switch.
 *
 * Pins are in app_config.h.
 */

#pragma once

#include <esp_err.h>

namespace antenna {

/** Select the external antenna. The pins are driven while awake and released
 * in light sleep.
 *
 * Call first in app_main(): the RF path has to be settled before anything
 * brings a radio up. */
esp_err_t init(void);

}  // namespace antenna
