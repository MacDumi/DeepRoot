#pragma once

#include <esp_err.h>

namespace factory_info {

/* Install a DeviceInstanceInfoProvider serving VendorName / ProductName /
 * HardwareVersion, and a SerialNumber derived from the chip's eFuse MAC.
 *
 * MUST be called before esp_matter::start() — the stack latches the provider
 * during chip_init(), so a later call is silently ignored.
 *
 * Never fails the boot. Every value is compiled in or read from eFuse, so
 * there is nothing to provision: a board reports a unique serial straight off
 * a plain app flash. */
esp_err_t init();

}  // namespace factory_info
