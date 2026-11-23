#include "ota_rollback.h"

#include <cinttypes>

#include <esp_log.h>
#include <esp_ota_ops.h>

#include <app/server/Server.h>
#include <platform/CHIPDeviceLayer.h>

#include "app_config.h"

namespace ota_rollback {
namespace {

const char *TAG = "ota_rollback";

using namespace app_config::ota;

/* Matter task only. */
uint32_t s_elapsed_s = 0;
uint32_t s_attached_s = 0;

void confirm(const char *why)
{
    const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "new firmware confirmed (%s)", why);
    } else {
        ESP_LOGE(TAG, "could not confirm new firmware: %s", esp_err_to_name(err));
    }
}

/* Matter task, from the SystemLayer timer: IsThreadAttached() and the fabric
 * table both expect the CHIP stack lock, which this context holds. */
void check(chip::System::Layer *layer, void *arg)
{
    s_elapsed_s += kCheckIntervalSec;

    /* OTA needs a fabric, so this only happens if the device was factory
     * reset during the trial. Even then the reset reboots and the bootloader
     * rolls back first; this is just a guard against waiting for an attach
     * that cannot come. */
    if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
        confirm("not commissioned; no network to test against");
        return;
    }

    /* Continuously attached, not attached once: an image that attaches and
     * then falls over straight away should not pass. */
    if (chip::DeviceLayer::ConnectivityMgr().IsThreadAttached()) {
        s_attached_s += kCheckIntervalSec;
        if (s_attached_s >= kHealthyAttachedSec) {
            confirm("attached to Thread");
            return;
        }
    } else {
        s_attached_s = 0;
    }

    if (s_elapsed_s >= kTrialTimeoutSec) {
        ESP_LOGE(TAG, "new firmware not attached to Thread for %" PRIu32 " s within %" PRIu32
                      " s; rolling back",
                 kHealthyAttachedSec, kTrialTimeoutSec);
        /* Reboots into the previous image; only returns on failure. */
        const esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
        ESP_LOGE(TAG, "rollback failed (%s); keeping this firmware", esp_err_to_name(err));
        /* Nothing valid to go back to. Left unconfirmed, the next reset would
         * abort this image too. */
        confirm("no previous firmware to roll back to");
        return;
    }

    layer->StartTimer(chip::System::Clock::Seconds32(kCheckIntervalSec), check, nullptr);
}

/* Matter task. */
void arm(intptr_t arg)
{
    chip::DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Seconds32(kCheckIntervalSec),
                                                check, nullptr);
}

}  // namespace

void start(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;

    /* Fails for an image flashed over USB, which has no OTA state: not on
     * trial. */
    if (running == nullptr || esp_ota_get_state_partition(running, &state) != ESP_OK ||
        state != ESP_OTA_IMG_PENDING_VERIFY) {
        return;
    }

    ESP_LOGW(TAG, "new firmware on trial: confirmed after %" PRIu32 " s attached to Thread, "
                  "rolled back if that has not happened within %" PRIu32 " s",
             kHealthyAttachedSec, kTrialTimeoutSec);
    chip::DeviceLayer::PlatformMgr().ScheduleWork(arm, 0);
}

}  // namespace ota_rollback
