/*
 * Device identity for the Basic Information cluster.
 */

#include "factory_info.h"

#include <cstdio>
#include <cstring>

#include <esp_log.h>
#include <esp_mac.h>
#include <sdkconfig.h>

#include <esp_matter_providers.h>
#include <lib/support/CodeUtils.h>
#include <platform/DeviceInstanceInfoProvider.h>

#include "app_config.h"

#if !CONFIG_CUSTOM_DEVICE_INSTANCE_INFO_PROVIDER
#error "factory_info requires CONFIG_CUSTOM_DEVICE_INSTANCE_INFO_PROVIDER=y"
#endif

namespace factory_info {
namespace {

const char *TAG = "factory_info";

using namespace app_config::factory;

/* Serial number derived from the eFuse MAC, which is burned by Espressif in
 * production and unique per chip.
 */
char s_serial[24] = "SOIL-UNKNOWN";

void derive_serial_from_mac()
{
    uint8_t mac[6] = {};
    const esp_err_t err = esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "eFuse MAC unreadable (%s); serial stays %s",
                 esp_err_to_name(err), s_serial);
        return;
    }

    snprintf(s_serial, sizeof(s_serial), "%s%02X%02X%02X%02X%02X%02X",
             kSerialPrefix, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

CHIP_ERROR copy_out(const char *src, char *buf, size_t buf_size)
{
    const size_t len = strlen(src);
    VerifyOrReturnError(buf_size > len, CHIP_ERROR_BUFFER_TOO_SMALL);
    memcpy(buf, src, len + 1);
    return CHIP_NO_ERROR;
}

class FactoryInfoProvider : public chip::DeviceLayer::DeviceInstanceInfoProvider {
public:
    CHIP_ERROR GetVendorName(char *buf, size_t size) override
    {
        return copy_out(kVendorName, buf, size);
    }

    CHIP_ERROR GetProductName(char *buf, size_t size) override
    {
        return copy_out(kProductName, buf, size);
    }

    CHIP_ERROR GetSerialNumber(char *buf, size_t size) override
    {
        return copy_out(s_serial, buf, size);
    }

    CHIP_ERROR GetHardwareVersionString(char *buf, size_t size) override
    {
        return copy_out(kHwVerString, buf, size);
    }

    CHIP_ERROR GetHardwareVersion(uint16_t &out) override
    {
        out = CONFIG_DEFAULT_DEVICE_HARDWARE_VERSION;
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR GetVendorId(uint16_t &out) override
    {
        out = CONFIG_DEVICE_VENDOR_ID;
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR GetProductId(uint16_t &out) override
    {
        out = CONFIG_DEVICE_PRODUCT_ID;
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR GetPartNumber(char *, size_t) override { return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE; }
    CHIP_ERROR GetProductURL(char *, size_t) override { return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE; }
    CHIP_ERROR GetProductLabel(char *, size_t) override { return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE; }

    /* Note this one does not yield an empty string: the cluster substitutes
     * 2020-01-01 for an absent manufacturing date. */
    CHIP_ERROR GetManufacturingDate(uint16_t &, uint8_t &, uint8_t &) override
    {
        return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
    }

    CHIP_ERROR GetRotatingDeviceIdUniqueId(chip::MutableByteSpan &) override
    {
        return CHIP_ERROR_NOT_IMPLEMENTED;
    }
};

FactoryInfoProvider s_provider;

}  // namespace

esp_err_t init()
{
    derive_serial_from_mac();
    esp_matter::set_custom_device_instance_info_provider(&s_provider);
    ESP_LOGI(TAG, "identity: %s %s, serial %s", kVendorName, kProductName, s_serial);
    return ESP_OK;
}

}  // namespace factory_info
