/*
 * Commissioning policy: keep an unprovisioned device discoverable, and return
 * a decommissioned one to setup mode.
 */

#pragma once

#include <esp_matter.h>

namespace commissioning {

/** Device event callback for esp_matter::start(). */
void on_device_event(const ChipDeviceEvent *event, intptr_t arg);

/** After esp_matter::start(): open the commissioning window if there is no
 * fabric, and print the onboarding codes. */
void start(void);

}  // namespace commissioning
