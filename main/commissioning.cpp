#include "commissioning.h"

#include <esp_log.h>
#include <esp_system.h>

#include <app/server/Server.h>
#include <setup_payload/OnboardingCodesUtil.h>

#include "ble_gate.h"

namespace commissioning {

static const char *TAG = "commissioning";

static void restart_for_commissioning(chip::System::Layer *layer, void *arg)
{
    esp_restart();
}

/* Put the device into or back into the setup mode if it has no fabric.
 *
 * Runs on the Matter task via ScheduleWork. */
static void ensure_commissioning_window(intptr_t arg)
{
    auto &server = chip::Server::GetInstance();

    const uint8_t fabrics = server.GetFabricTable().FabricCount();
    if (fabrics != 0) {
        ESP_LOGI(TAG, "commissioned on %u fabric(s)", fabrics);
        return;
    }

    /* No fabric, but this boot started with one, so the BLE controller was
     * never brought up (see ble_gate.h).
     *
     * Delayed so a RemoveFabric response still in flight over Thread gets out
     * before the reset. A repeat call just restarts the same timer. */
    if (ble_gate::suppressed()) {
        ESP_LOGW(TAG, "no fabric left and BLE is off this boot; restarting to commission");
        chip::DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Seconds32(2),
                                                    restart_for_commissioning, nullptr);
        return;
    }

    auto &window = server.GetCommissioningWindowManager();
    if (!window.IsCommissioningWindowOpen()) {
        CHIP_ERROR err = window.OpenBasicCommissioningWindow();
        if (err != CHIP_NO_ERROR) {
            ESP_LOGE(TAG, "failed to open commissioning window: %" CHIP_ERROR_FORMAT,
                     err.Format());
            return;
        }
    }

    ESP_LOGI(TAG, "not provisioned -- commissioning window open, pair with:");
    PrintOnboardingCodes(
        chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE));
}

void on_device_event(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "commissioning complete");
        break;

    /* The window closed: either it timed out or commissioning was abandoned.
     * Re-arm if we are still unprovisioned; the fabric check inside is what
     * stops this from fighting the normal post-commissioning shutdown. */
    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
    /* Decommissioned -- the last fabric is gone, so go back to setup mode. */
    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        chip::DeviceLayer::PlatformMgr().ScheduleWork(ensure_commissioning_window, 0);
        break;

    default:
        break;
    }

}

void start(void)
{
    /* esp_matter::start() blocks until chip::Server::Init() has run, so the
     * fabric table and the window manager are both live by now. */
    chip::DeviceLayer::PlatformMgr().ScheduleWork(ensure_commissioning_window, 0);
}

}  // namespace commissioning
