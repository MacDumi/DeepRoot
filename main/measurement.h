/*
 * Periodic measurement and publishing of soil moisture and battery level.
 */

#pragma once

#include <esp_err.h>
#include <esp_matter.h>
#include <stdint.h>

namespace measurement {

/** Start the periodic measurement task, publishing to the given endpoint.
 *
 * The interval adapts to the soil moisture value. */
esp_err_t start(uint16_t endpoint_id, nullable<uint8_t> boot_moisture,
                nullable<uint8_t> boot_battery);

/** Read soil moisture now and publish it. Blocks for the probe settle time.
 *
 * @return 0-100 percent, or null if the reading is not trustworthy. */
nullable<uint8_t> update_moisture(void);

}  // namespace measurement
