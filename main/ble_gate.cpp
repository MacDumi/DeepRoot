#include "ble_gate.h"

#include <esp_bt.h>
#include <esp_err.h>
#include <esp_log.h>

#include <lib/core/TLV.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/KeyValueStoreManager.h>

namespace ble_gate {
namespace {

const char *TAG = "ble_gate";

bool s_suppressed = false;

/* FabricTable's index list, as written by FabricTable::StoreFabricIndexInfo():
 * an anonymous structure holding the next free index (tag 0) and an array of
 * committed fabric indices (tag 1). */
constexpr char kFabricIndexKey[] = "g/fidx";
constexpr chip::TLV::Tag kFabricIndicesTag = chip::TLV::ContextTag(1);

constexpr size_t kIndexInfoMaxSize = 64;

/* Number of committed fabrics, or -1 if it cannot be determined. */
int persisted_fabric_count(void)
{
    using namespace chip::TLV;

    uint8_t buf[kIndexInfoMaxSize];
    size_t len = 0;
    CHIP_ERROR err = chip::DeviceLayer::PersistedStorage::KeyValueStoreMgr().Get(
        kFabricIndexKey, buf, sizeof(buf), &len);
    if (err == CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND) {
        return 0;
    }
    if (err != CHIP_NO_ERROR) {
        ESP_LOGW(TAG, "fabric index unreadable: %" CHIP_ERROR_FORMAT, err.Format());
        return -1;
    }

    ContiguousBufferTLVReader reader;
    reader.Init(buf, len);

    TLVType outer;
    if (reader.Next(kTLVType_Structure, AnonymousTag()) != CHIP_NO_ERROR ||
        reader.EnterContainer(outer) != CHIP_NO_ERROR) {
        return -1;
    }

    while (reader.Next() == CHIP_NO_ERROR) {
        if (reader.GetTag() != kFabricIndicesTag) {
            continue;
        }

        TLVType inner;
        if (reader.GetType() != kTLVType_Array || reader.EnterContainer(inner) != CHIP_NO_ERROR) {
            return -1;
        }

        int count = 0;
        while ((err = reader.Next()) == CHIP_NO_ERROR) {
            ++count;
        }
        return err == CHIP_END_OF_TLV ? count : -1;
    }

    return -1;
}

void release_memory_cb(chip::System::Layer *layer, void *arg)
{
    esp_err_t err = esp_bt_mem_release(ESP_BT_MODE_BLE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_bt_mem_release failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "BLE memory returned to the heap");
}

/* Runs on the Matter task. Deferred rather than immediate: esp_matter's
 * deinit_ble_if_commissioned() still has to walk CHIP's BLE manager through
 * DeinitBLE(), which reads NimBLE host state (ble_hs_is_enabled()) out of the
 * very .bss esp_bt_mem_release() hands to the heap. Both are queued work items
 * that finish within milliseconds; the 2 s is the same margin CHIP's own
 * ClaimBLEMemory() uses. */
void arm_release(intptr_t arg)
{
    chip::DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Seconds32(2),
                                                release_memory_cb, nullptr);
}

}  // namespace

void init(void)
{
    const int fabrics = persisted_fabric_count();
    s_suppressed = fabrics > 0;

    if (s_suppressed) {
        ESP_LOGI(TAG, "%d fabric(s) persisted; BLE controller stays off this boot", fabrics);
    } else if (fabrics == 0) {
        ESP_LOGI(TAG, "no fabric persisted; BLE enabled for commissioning");
    } else {
        ESP_LOGW(TAG, "fabric table unreadable; BLE enabled to be safe");
    }
}

bool suppressed(void)
{
    return s_suppressed;
}

void release_memory(void)
{
    if (s_suppressed) {
        chip::DeviceLayer::PlatformMgr().ScheduleWork(arm_release, 0);
    }
}

}  // namespace ble_gate

/* Called from CHIP's BLEManagerImpl::InitESPBleLayer(). */
extern "C" esp_err_t __real_nimble_port_init(void);

extern "C" esp_err_t __wrap_nimble_port_init(void)
{
    if (ble_gate::suppressed()) {
        ESP_LOGI("ble_gate", "nimble_port_init refused: device is commissioned");
        return ESP_ERR_NOT_SUPPORTED;
    }
    return __real_nimble_port_init();
}
