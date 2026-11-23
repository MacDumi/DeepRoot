/*
 * Rollback for OTA updates that do not work.
 *
 * With CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE, an image installed by OTA boots
 * in the PENDING_VERIFY state. If the device resets before the image is
 * confirmed, through a crash, a watchdog or a power cut, the bootloader marks
 * it aborted and boots the previous image instead. Without a confirmation the
 * new image would never survive its first reboot.
 *
 * This module is the confirmation. The new image is accepted once the node has
 * stayed attached to Thread for kHealthyAttachedSec, and rolled back if that
 * has not happened within kTrialTimeoutSec. Attaching proves the radio, the
 * Thread stack and the stored network credentials all survived the update.
 *
 * Images flashed over USB start in no OTA state at all and are never on
 * trial, so nothing here runs for them.
 */

#pragma once

namespace ota_rollback {

/** After esp_matter::start(): put a freshly installed OTA image on trial. A
 * no-op for every other boot. */
void start(void);

}  // namespace ota_rollback
