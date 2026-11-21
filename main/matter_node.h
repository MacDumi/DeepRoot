/*
 * The Matter node: data model construction and stack start-up.
 *
 * Split in two because the halves have different preconditions: create()
 * builds the data model and may only run before the stack exists; start()
 * brings up Thread, BLE (if needed) and the Matter server.
 */

#pragma once

#include <esp_err.h>
#include <esp_matter.h>
#include <stdint.h>

namespace matter_node {

/** Build the root node, the soil sensor endpoint and the device identity.
 *
 * @param moisture  boot moisture reading, seeds the attribute (null = unknown)
 * @param battery   boot battery reading, seeds the attribute (null = unknown)
 */
esp_err_t create(nullable<uint8_t> moisture, nullable<uint8_t> battery);

/** Start the Matter stack. After create(). */
esp_err_t start(void);

/** Endpoint id of the soil sensor. Valid after create(). */
uint16_t soil_endpoint_id(void);

}  // namespace matter_node
