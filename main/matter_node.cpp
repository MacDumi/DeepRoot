#include "matter_node.h"

#include <esp_event.h>
#include <esp_ieee802154.h>
#include <esp_log.h>

#include <esp_openthread_types.h>
#include <platform/ESP32/OpenthreadLauncher.h>

#include "app_config.h"
#include "ble_gate.h"
#include "commissioning.h"
#include "factory_info.h"
#include "icd_support.h"
#include "soil_endpoint.h"

using namespace esp_matter;

namespace matter_node {

static const char *TAG = "matter_node";

static uint16_t s_soil_endpoint_id = 0;

static esp_err_t identification_cb(identification::callback_type_t type, uint16_t endpoint_id,
                                   uint8_t effect_id, uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "identify: type %u endpoint %u", type, endpoint_id);
    /* TODO: blink the on-board LED so a user can tell which sensor a
     * controller is talking to. */
    return ESP_OK;
}

static esp_err_t attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id,
                                     uint32_t cluster_id, uint32_t attribute_id,
                                     esp_matter_attr_val_t *val, void *priv_data)
{
    /* A soil sensor is read-only, so nothing to action here. Kept because
     * node::create() requires the callback. */
    return ESP_OK;
}

/* esp-matter's Basic Information cluster omits SerialNumber, so
 * cluster::basic_information::create() never instantiates it
 * and controllers show the field blank no matter what the provider knows.
 *
 * ATTRIBUTE_FLAG_MANAGED_INTERNALLY (set inside create_serial_number) means
 * the value is served from the DeviceInstanceInfoProvider on every read, not
 * stored in esp-matter's attribute store, hence the NULL/0 value here. */
static void add_serial_number_attribute(node_t *node)
{
    constexpr uint32_t kBasicInformationClusterId = 0x0028;

    endpoint_t *root = endpoint::get(node, 0);
    if (root == nullptr) {
        ESP_LOGE(TAG, "no root endpoint; serial number not published");
        return;
    }

    cluster_t *basic = cluster::get(root, kBasicInformationClusterId);
    if (basic == nullptr) {
        ESP_LOGE(TAG, "no basic information cluster; serial number not published");
        return;
    }

    cluster::basic_information::attribute::create_serial_number(basic, nullptr, 0);
}

/* Hand the OpenThread port layer its configuration. */
static esp_err_t openthread_platform_init(void)
{
    static const char kThreadStoragePartition[] = "nvs";

    esp_openthread_platform_config_t config = {};

    config.radio_config.radio_mode = RADIO_MODE_NATIVE;
    config.host_config.host_connection_mode = HOST_CONNECTION_MODE_NONE;
    config.port_config.storage_partition_name = kThreadStoragePartition;
    config.port_config.netif_queue_size = 10;
    config.port_config.task_queue_size = 10;

    return set_openthread_platform_config(&config);
}

/* Lower the 802.15.4 TX power before Thread sends its first frame.
 *
 * OPENTHREAD_EVENT_START is posted once the radio is enabled and before
 * StartThreadTask() launches the ot_task, so nothing has gone on air yet. The
 * default event loop task outranks the main task, so the handler runs the
 * moment the event is posted. */
static void on_openthread_start(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_ieee802154_set_txpower(app_config::radio::kThreadTxPowerDbm);
    ESP_LOGI(TAG, "802.15.4 TX power set to %d dBm", esp_ieee802154_get_txpower());
}

static esp_err_t register_tx_power_handler(void)
{
    /* esp_matter::start() creates the default loop too, and accepts it already
     * existing. */
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    return esp_event_handler_register(OPENTHREAD_EVENT, OPENTHREAD_EVENT_START,
                                      on_openthread_start, nullptr);
}

esp_err_t create(nullable<uint8_t> moisture, nullable<uint8_t> battery)
{
    /* Installs the identity provider. Must precede esp_matter::start(), which
     * latches the provider set during chip_init(). */
    esp_err_t err = factory_info::init();
    if (err != ESP_OK) {
        return err;
    }

    /* Before node::create(): the ICD Management cluster is built onto the root
     * endpoint from this configuration. */
    err = icd_support::configure();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ICD configuration failed: %s", esp_err_to_name(err));
        return err;
    }

    node::config_t node_config;
    icd_support::describe_user_active_trigger(node_config);

    node_t *node = node::create(&node_config, attribute_update_cb, identification_cb);
    if (node == nullptr) {
        ESP_LOGE(TAG, "failed to create Matter node");
        return ESP_FAIL;
    }

    add_serial_number_attribute(node);

    return soil_sensor::create(node, moisture, battery, &s_soil_endpoint_id);
}

esp_err_t start(void)
{
    /* Before start(), which initialises the Thread stack and asserts if this
     * has not run. See openthread_platform_init(). */
    esp_err_t err = openthread_platform_init();
    if (err != ESP_OK) {
        return err;
    }

    /* Before start(), which enables the radio. See on_openthread_start(). */
    err = register_tx_power_handler();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TX power handler registration failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Before start(), which brings the BLE controller up unless this has
     * decided against it. See ble_gate.h. */
    ble_gate::init();

    err = esp_matter::start(commissioning::on_device_event);
    if (err != ESP_OK) {
        return err;
    }

    ble_gate::release_memory();
    commissioning::start();
    icd_support::start();
    return ESP_OK;
}

uint16_t soil_endpoint_id(void)
{
    return s_soil_endpoint_id;
}

}  // namespace matter_node
