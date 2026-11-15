/*
 * Matter Soil Sensor endpoint
 *
 * esp_matter 1.5.x ships NO soil sensor device type and NO soil cluster
 * helper -- but connectedhomeip does implement the SoilMeasurement cluster
 * (0x0430, soil-measurement-server) and esp_matter's Kconfig exposes
 * SUPPORT_SOIL_MEASUREMENT_CLUSTER (default y).
 *
 * The device type is assembled here from esp_matter's generic
 * endpoint/cluster/attribute APIs.
 */

#pragma once

#include <esp_err.h>
#include <esp_matter.h>
#include <stdint.h>

#include <clusters/PowerSource/Enums.h>

namespace soil_sensor {

/* SoilMeasurement cluster, Matter 1.5. */
constexpr uint32_t kSoilMeasurementClusterId = 0x00000430;
constexpr uint32_t kSoilMoistureMeasurementLimitsAttributeId = 0x00000000;
constexpr uint32_t kSoilMoistureMeasuredValueAttributeId = 0x00000001;

constexpr uint32_t kSoilSensorDeviceTypeId = 0x0045;
constexpr uint8_t kSoilSensorDeviceTypeVersion = 1;

/* PowerSource cluster */
constexpr uint32_t kPowerSourceClusterId = 0x0000002F;
constexpr uint32_t kBatPercentRemainingAttributeId = 0x0000000C;
constexpr uint32_t kBatChargeLevelAttributeId = 0x0000000E;
constexpr uint32_t kBatReplacementNeededAttributeId = 0x0000000F;

/** Create a soil sensor endpoint on the given node.
 *
 * Adds Descriptor, Identify, SoilMeasurement and PowerSource (server)
 * clusters.
 *
 * The attributes start at the given readings rather than null.
 *
 * @param node      the Matter node to attach to
 * @param moisture  initial moisture, 0-100, or null for unknown
 * @param battery   initial battery charge, 0-100, or null for unknown
 * @param[out] endpoint_id  id assigned to the created endpoint
 * @return ESP_OK on success
 */
esp_err_t create(esp_matter::node_t *node, nullable<uint8_t> moisture,
                 nullable<uint8_t> battery, uint16_t *endpoint_id);

/** Publish a soil moisture reading.
 *
 * @param endpoint_id  endpoint ID as returned by create()
 * @param percent      0-100, or a null nullable to report "unknown"
 *                     (e.g. probe disconnected or reading out of range)
 */
esp_err_t report_moisture(uint16_t endpoint_id,
                          nullable<uint8_t> percent);

/** Publish battery level.
 *
 * Updates BatPercentRemaining plus the two coarse attributes derived from it,
 * BatChargeLevel and BatReplacementNeeded -- see the threshold constants above.
 *
 * @param endpoint_id  endpoint ID as returned by create()
 * @param percent      0-100 as an ordinary percentage, or a null nullable
 *                     for "unknown". The half-percent encoding the Matter
 *                     attribute uses is applied internally. A null leaves
 *                     BatChargeLevel and BatReplacementNeeded untouched.
 */
esp_err_t report_battery(uint16_t endpoint_id, nullable<uint8_t> percent);

/** The BatChargeLevel that report_battery() publishes for a percentage. */
chip::app::Clusters::PowerSource::BatChargeLevelEnum charge_level(uint8_t percent);

}  // namespace soil_sensor
