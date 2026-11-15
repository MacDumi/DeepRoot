#include "soil_endpoint.h"

#include <cstdio>

#include <esp_log.h>
#include <esp_matter.h>

#include <app/AttributeAccessInterface.h>
#include <app/AttributeAccessInterfaceRegistry.h>
#include <clusters/SoilMeasurement/Attributes.h>
#include <clusters/SoilMeasurement/Metadata.h>

#include "app_config.h"

using namespace esp_matter;
using namespace esp_matter::cluster;

namespace soil_sensor {

static const char *TAG = "soil_endpoint";

namespace PowerSourceNs = chip::app::Clusters::PowerSource;
namespace SoilNs = chip::app::Clusters::SoilMeasurement;
namespace GlobalsNs = chip::app::Clusters::Globals;

/* ---- SoilMoistureMeasurementLimits -------------------------------------- */
static const GlobalsNs::Structs::MeasurementAccuracyRangeStruct::Type kMoistureAccuracyRanges[] = {
    {
        .rangeMin = 0,
        .rangeMax = 100,
        .fixedMax = chip::MakeOptional(app_config::probe::kAccuracyFixedMaxPercent),
    },
};

static const SoilNs::Attributes::SoilMoistureMeasurementLimits::TypeInfo::Type kMoistureLimits = {
    .measurementType = GlobalsNs::MeasurementTypeEnum::kSoilMoisture,
    .measured = true,
    .minMeasuredValue = 0,
    .maxMeasuredValue = 100,
    .accuracyRanges = chip::app::DataModel::List<
        const GlobalsNs::Structs::MeasurementAccuracyRangeStruct::Type>(kMoistureAccuracyRanges),
};

class limits_access_t : public chip::app::AttributeAccessInterface {
public:
    /* Any endpoint: this device has one soil endpoint, and its id is not known
     * until endpoint::create() has run. */
    limits_access_t() : AttributeAccessInterface(chip::NullOptional, SoilNs::Id) {}

    CHIP_ERROR Read(const chip::app::ConcreteReadAttributePath &path,
                    chip::app::AttributeValueEncoder &encoder) override
    {
        if (path.mAttributeId != SoilNs::Attributes::SoilMoistureMeasurementLimits::Id) {
            return CHIP_NO_ERROR;  // not encoded: served from the attribute store
        }
        return encoder.Encode(kMoistureLimits);
    }
};

static limits_access_t s_limits_access;

/* BatPercentRemaining is in HALF-PERCENT units: 0-200 */
static nullable<uint8_t> to_half_percent(nullable<uint8_t> percent)
{
    if (percent.is_null()) {
        return percent;
    }
    const uint8_t p = percent.value() > 100 ? 100 : percent.value();
    return nullable<uint8_t>(static_cast<uint8_t>(p * 2));
}

PowerSourceNs::BatChargeLevelEnum charge_level(uint8_t percent)
{
    if (percent <= app_config::battery::kBatteryCriticalPercent) {
        return PowerSourceNs::BatChargeLevelEnum::kCritical;
    }
    if (percent <= app_config::battery::kBatteryWarningPercent) {
        return PowerSourceNs::BatChargeLevelEnum::kWarning;
    }
    return PowerSourceNs::BatChargeLevelEnum::kOk;
}

esp_err_t create(node_t *node, nullable<uint8_t> moisture, nullable<uint8_t> battery,
                 uint16_t *endpoint_id)
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

    /* SoilMeasurement -- no esp_matter helper exists, so build it by id. */
    cluster_t *soil = cluster::create(endpoint, kSoilMeasurementClusterId,
                                      CLUSTER_FLAG_SERVER);
    if (soil == nullptr) {
        ESP_LOGE(TAG, "soil measurement cluster create failed");
        return ESP_FAIL;
    }

    /* A bare cluster::create() adds no global attributes, and both are
     * mandatory on every cluster. No SoilMeasurement features exist. */
    global::attribute::create_cluster_revision(soil, SoilNs::kRevision);
    global::attribute::create_feature_map(soil, 0);

    /* SoilMoistureMeasurementLimits: listed in the store so it appears in the
     * AttributeList, but the value always comes from s_limits_access. The
     * empty array is esp_matter's placeholder for an internally managed attribute. */
    attribute::create(soil, kSoilMoistureMeasurementLimitsAttributeId,
                      ATTRIBUTE_FLAG_MANAGED_INTERNALLY, esp_matter_array(nullptr, 0, 0));
    if (!chip::app::AttributeAccessInterfaceRegistry::Instance().Register(&s_limits_access)) {
        ESP_LOGE(TAG, "soil measurement limits access already registered");
        return ESP_ERR_INVALID_STATE;
    }

    /* SoilMoistureMeasuredValue: nullable percent (0-100), seeded with the
     * boot reading so the first report after a reboot */
    attribute::create(soil, kSoilMoistureMeasuredValueAttributeId,
                      ATTRIBUTE_FLAG_NULLABLE, esp_matter_nullable_uint8(moisture));

    /* PowerSource, battery flavour.
     * endpoint::power_source::add() composes the PowerSource device type and
     * its cluster onto an endpoint that already has one.
     */
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

    if (!battery.is_null()) {
        const PowerSourceNs::BatChargeLevelEnum level = charge_level(battery.value());
        power_config.power_source.features.battery.bat_charge_level = chip::to_underlying(level);
        power_config.power_source.features.battery.bat_replacement_needed =
            level == PowerSourceNs::BatChargeLevelEnum::kCritical;
    }

    /* Creates the three attributes the Battery feature makes mandatory:
     * BatChargeLevel, BatReplacementNeeded and BatReplaceability.
     */
    esp_err_t err = endpoint::power_source::add(endpoint, &power_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "power source add failed: %d", err);
        return err;
    }

    /* BatPercentRemaining is optional, so the feature helper does not create
     * it. Seeded from the boot reading.
     */
    cluster_t *power = cluster::get(endpoint, kPowerSourceClusterId);
    if (power == nullptr) {
        ESP_LOGE(TAG, "power source cluster missing after add");
        return ESP_FAIL;
    }

    cluster::power_source::attribute::create_bat_percent_remaining(
        power, to_half_percent(battery), nullable<uint8_t>(0), nullable<uint8_t>(200));

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
    esp_matter_attr_val_t pct_val = esp_matter_nullable_uint8(to_half_percent(percent));
    esp_err_t err = attribute::update(endpoint_id, kPowerSourceClusterId,
                                      kBatPercentRemainingAttributeId, &pct_val);

    /* A null percent means the reading was not trustworthy. Leave both
     * attributes at their previous value in that case.
     */
    if (percent.is_null()) {
        return err;
    }

    const PowerSourceNs::BatChargeLevelEnum level = charge_level(percent.value());

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
