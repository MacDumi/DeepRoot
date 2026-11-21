#include "icd_support.h"

#include <cinttypes>
#include <cstdio>

#include <esp_log.h>

#ifdef CONFIG_ENABLE_ICD_SERVER
#include <access/AuthMode.h>
#include <access/SubjectDescriptor.h>
#include <app/InteractionModelEngine.h>
#include <app/ReadHandler.h>
#include <app/icd/server/ICDConfigurationData.h>
#include <app/icd/server/ICDMonitoringTable.h>
#include <app/icd/server/ICDNotifier.h>
#include <app/server/Server.h>
#include <clusters/IcdManagement/Enums.h>
#include <esp_matter_icd_configuration.h>
#include <lib/support/TypeTraits.h>
#include <platform/CHIPDeviceLayer.h>
#endif

#include "app_config.h"

namespace icd_support {

#ifdef CONFIG_ENABLE_ICD_SERVER

namespace {

const char *TAG = "icd_support";

using namespace app_config::icd;

/* User Active Mode Trigger. LIT advertises the UAT feature - ENABLE_ICD_LIT
 * selects ENABLE_ICD_USER_ACTIVE_MODE_TRIGGER.
 *
 * Runs on the Matter task: ICDNotifier walks its subscriber list and drives the
 * ICD state machine, neither of which is safe from the button task. */
void notify_user_active_work(intptr_t arg)
{
    chip::app::ICDNotifier::GetInstance().NotifyNetworkActivityNotification();
}

/* ---------------------------------------------------------------------
 * Check in the moment a subscription is lost for good.
 *
 * Without this a LIT ICD that loses its subscription is invisible to the
 * controller until the next Idle->ActiveMode transition - kIcdIdleModeSec, an
 * hour.
 *
 * The case that matters is a controller that gave up on the subscription while
 * the node was powered off. On boot the node resumes CASE -- Sigma2Resume
 * succeeds, the radio is fine -- and replays its subscription id. The controller
 * no longer has it and answers StatusResponse INVALID_SUBSCRIPTION (0x7d).
 * ReadHandler::OnMessageReceived() turns that into a bare Close(), whose
 * default is kDropPersistedSubscription, so the persisted entry is deleted.
 * That is correct and it lifts the Check-In suppression in
 * ShouldCheckInMsgsBeSentAtActiveModeFunction() - but nothing then asks for a
 * Check-In, and the node goes quiet for the rest of the hour.
 * ------------------------------------------------------------------- */

/* Re-runs the Check-In decision for every registered ICD client. */
void reevaluate_check_ins(intptr_t arg)
{
    auto &server = chip::Server::GetInstance();
    auto *engine = chip::app::InteractionModelEngine::GetInstance();
    auto *keystore = server.GetSessionKeystore();

    const uint16_t limit =
        chip::ICDConfigurationData::GetInstance().GetClientsSupportedPerFabric();

    for (const auto &fabric : server.GetFabricTable()) {
        chip::ICDMonitoringTable table(server.GetPersistentStorage(), fabric.GetFabricIndex(),
                                       limit, keystore);
        if (table.IsEmpty()) {
            continue;
        }

        for (uint16_t i = 0; i < table.Limit(); i++) {
            chip::ICDMonitoringEntry entry(keystore);

            CHIP_ERROR err = table.Get(i, entry);
            if (err == CHIP_ERROR_NOT_FOUND) {
                break;
            }
            if (err != CHIP_NO_ERROR) {
                continue;
            }

            const chip::NodeId subject = static_cast<chip::NodeId>(entry.monitoredSubject);

            if (engine->SubjectHasActiveSubscription(entry.fabricIndex, subject) ||
                engine->SubjectHasPersistedSubscription(entry.fabricIndex, subject)) {
                continue;
            }

            ESP_LOGI(TAG, "nothing subscribed for 0x%016" PRIX64 " (fabric %u), checking in",
                     subject, entry.fabricIndex);

            chip::Access::SubjectDescriptor descriptor = {};
            descriptor.fabricIndex = entry.fabricIndex;
            descriptor.authMode = chip::Access::AuthMode::kCase;
            descriptor.subject = subject;

            chip::app::ICDNotifier::GetInstance().NotifySendCheckIn(descriptor);
        }
    }
}

class subscription_check_in_t : public chip::app::ReadHandler::ApplicationCallback {
public:
    void OnSubscriptionTerminated(chip::app::ReadHandler &handler) override
    {
        /* Deferred, not done here. This runs from ~ReadHandler(), and
         * SubjectHasActiveSubscription() walks the very pool the handler is
         * mid-release from.
         *
         * Both ends are on the Matter task, so there is no lock to take and no
         * ordering question beyond this one. */
        chip::DeviceLayer::PlatformMgr().ScheduleWork(reevaluate_check_ins, 0);
    }
};

subscription_check_in_t s_subscription_check_in;

void register_subscription_check_in(intptr_t arg)
{
    chip::app::InteractionModelEngine::GetInstance()->RegisterReadHandlerAppCallback(
        &s_subscription_check_in);
    ESP_LOGI(TAG, "check-in on subscription loss armed");
}

}  // namespace

esp_err_t configure(void)
{
    esp_matter::icd::config_t config = {};

    config.enable_icd_server = true;
    config.slow_interval_ms = kIcdSlowPollMs;
    config.fast_interval_ms = kIcdFastPollMs;
    config.idle_mode_duration_s = kIcdIdleModeSec;
    config.active_mode_duration_ms = kIcdActiveModeMs;
    config.active_threshold_ms = kIcdActiveThresholdMs;

    return esp_matter::icd::set_configuration_data(&config);
}

void describe_user_active_trigger(esp_matter::node::config_t &config)
{
    /* kAppDefinedButton because the user button is a product-specific
     * control, not one of the standard setup controls the other hint bits
     * name, and kCustomInstruction because the free-text field is the only way
     * to say "briefly", as opposed to the long hold that factory-resets. */
    using chip::app::Clusters::IcdManagement::UserActiveModeTriggerBitmap;

    auto &uat = config.root_node.icd_user_active_mode_trigger;
    uat.user_active_mode_trigger_hint =
        chip::to_underlying(UserActiveModeTriggerBitmap::kAppDefinedButton) |
        chip::to_underlying(UserActiveModeTriggerBitmap::kCustomInstruction);
    snprintf(uat.user_active_mode_trigger_instruction,
             sizeof(uat.user_active_mode_trigger_instruction),
             "Press the button briefly");
}

void start(void)
{
    chip::DeviceLayer::PlatformMgr().ScheduleWork(register_subscription_check_in, 0);
}

void notify_user_active(void)
{
    ESP_LOGI(TAG, "user active mode trigger");
    chip::DeviceLayer::PlatformMgr().ScheduleWork(notify_user_active_work, 0);
}

#else  // !CONFIG_ENABLE_ICD_SERVER

esp_err_t configure(void)
{
    return ESP_OK;
}

void describe_user_active_trigger(esp_matter::node::config_t &config) {}

void start(void) {}

void notify_user_active(void) {}

#endif  // CONFIG_ENABLE_ICD_SERVER

}  // namespace icd_support
