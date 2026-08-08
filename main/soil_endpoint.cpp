#include "soil_endpoint.h"

#include <cstdio>

#include <esp_log.h>
#include <esp_matter.h>

using namespace esp_matter;
using namespace esp_matter::cluster;

namespace soil_sensor {

static const char *TAG = "soil_endpoint";

esp_err_t create(node_t *node, uint16_t *endpoint_id)
{
    if (node == nullptr || endpoint_id == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    endpoint_t *endpoint = endpoint::create(node, ENDPOINT_FLAG_NONE, nullptr);
    if (endpoint == nullptr) {
        ESP_LOGE(TAG, "endpoint::create failed");
        return ESP_FAIL;
    }

    /* Every endpoint needs a Descriptor cluster. */
    descriptor::config_t descriptor_config;
    if (descriptor::create(endpoint, &descriptor_config, CLUSTER_FLAG_SERVER) == nullptr) {
        ESP_LOGE(TAG, "descriptor cluster create failed");
        return ESP_FAIL;
    }

    /* Identify lets a controller ask the device to make itself known.
     * Required by the spec for most device types and cheap to include. */
    identify::config_t identify_config;
    if (identify::create(endpoint, &identify_config, CLUSTER_FLAG_SERVER) == nullptr) {
        ESP_LOGE(TAG, "identify cluster create failed");
        return ESP_FAIL;
    }

    /* SoilMeasurement — no esp_matter helper exists, so build it by id. */
    cluster_t *soil = cluster::create(endpoint, kSoilMeasurementClusterId,
                                      CLUSTER_FLAG_SERVER);
    if (soil == nullptr) {
        ESP_LOGE(TAG, "soil measurement cluster create failed");
        return ESP_FAIL;
    }

    /* SoilMoistureMeasuredValue: nullable percent (0-100).
     * Starts null — we have not read the probe yet, and reporting 0%
     * before the first measurement would be a lie a controller may act on. */
    nullable<uint8_t> unknown;
    attribute::create(soil, kSoilMoistureMeasuredValueAttributeId,
                      ATTRIBUTE_FLAG_NULLABLE, esp_matter_nullable_uint8(unknown));

    /* TODO: SoilMoistureMeasurementLimits (0x0000) is a MeasurementAccuracy
     * struct describing the probe's accuracy and measurable range. Maybe not needed here? */

    /* PowerSource, battery flavour.
     * endpoint::power_source::add() composes the PowerSource device type and
     * its cluster onto an endpoint that already has one.
     */
    namespace PowerSourceNs = chip::app::Clusters::PowerSource;

    endpoint::power_source::config_t power_config;
    power_config.power_source.feature_flags =
        cluster::power_source::feature::battery::get_id();
    power_config.power_source.status =
        chip::to_underlying(PowerSourceNs::PowerSourceStatusEnum::kActive);
    power_config.power_source.order = 0;
    snprintf(power_config.power_source.description,
             sizeof(power_config.power_source.description), "Battery");

    /* BatReplaceability is mandatory under the Battery feature */
    power_config.power_source.features.battery.bat_replaceability =
        chip::to_underlying(PowerSourceNs::BatReplaceabilityEnum::kUserReplaceable);

    /* Creates the three attributes the Battery feature makes mandatory:
     * BatChargeLevel, BatReplacementNeeded and BatReplaceability.
     */
    esp_err_t err = endpoint::power_source::add(endpoint, &power_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "power source add failed: %d", err);
        return err;
    }

    /* BatPercentRemaining is optional, so the feature helper does not create
     * it. Starts null as the first measurement has not happened yet.
     */
    cluster_t *power = cluster::get(endpoint, kPowerSourceClusterId);
    if (power == nullptr) {
        ESP_LOGE(TAG, "power source cluster missing after add");
        return ESP_FAIL;
    }

    nullable<uint8_t> unknown_percent;
    cluster::power_source::attribute::create_bat_percent_remaining(
        power, unknown_percent, nullable<uint8_t>(0), nullable<uint8_t>(200));

    err = endpoint::add_device_type(endpoint, kSoilSensorDeviceTypeId,
                                    kSoilSensorDeviceTypeVersion);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add_device_type failed: %d", err);
        return err;
    }

    *endpoint_id = endpoint::get_id(endpoint);
    ESP_LOGI(TAG, "soil sensor endpoint created, id %u", *endpoint_id);
    return ESP_OK;
}

esp_err_t report_moisture(uint16_t endpoint_id, nullable<uint8_t> percent)
{
    esp_matter_attr_val_t val = esp_matter_nullable_uint8(percent);
    return attribute::update(endpoint_id, kSoilMeasurementClusterId,
                             kSoilMoistureMeasuredValueAttributeId, &val);
}

esp_err_t report_battery(uint16_t endpoint_id, nullable<uint8_t> percent)
{
    namespace PowerSourceNs = chip::app::Clusters::PowerSource;

    /* BatPercentRemaining is in HALF-PERCENT units: 0-200 */
    nullable<uint8_t> half_percent;
    if (!percent.is_null()) {
        const uint8_t p = percent.value() > 100 ? 100 : percent.value();
        half_percent = nullable<uint8_t>(static_cast<uint8_t>(p * 2));
    }

    esp_matter_attr_val_t pct_val = esp_matter_nullable_uint8(half_percent);
    esp_err_t err = attribute::update(endpoint_id, kPowerSourceClusterId,
                                      kBatPercentRemainingAttributeId, &pct_val);

    /* A null percent means the reading was not trustworthy. Leave both
     * attributes at their previous value in that case.
     */
    if (percent.is_null()) {
        return err;
    }

    const uint8_t p = percent.value();
    PowerSourceNs::BatChargeLevelEnum level;
    if (p <= kBatteryCriticalPercent) {
        level = PowerSourceNs::BatChargeLevelEnum::kCritical;
    } else if (p <= kBatteryWarningPercent) {
        level = PowerSourceNs::BatChargeLevelEnum::kWarning;
    } else {
        level = PowerSourceNs::BatChargeLevelEnum::kOk;
    }

    esp_matter_attr_val_t level_val = esp_matter_enum8(chip::to_underlying(level));
    esp_err_t level_err = attribute::update(endpoint_id, kPowerSourceClusterId,
                                            kBatChargeLevelAttributeId, &level_val);

    /* Tied to Critical rather than to a threshold of its own. */
    esp_matter_attr_val_t replace_val =
        esp_matter_bool(level == PowerSourceNs::BatChargeLevelEnum::kCritical);
    esp_err_t replace_err = attribute::update(endpoint_id, kPowerSourceClusterId,
                                              kBatReplacementNeededAttributeId, &replace_val);

    /* Every attribute is attempted even if an earlier one failed. First error wins. */
    if (err != ESP_OK) {
        return err;
    }
    return level_err != ESP_OK ? level_err : replace_err;
}

}  // namespace soil_sensor
