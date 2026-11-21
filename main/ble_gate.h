/*
 * Keeps the BLE controller off on a boot that has nothing to commission.
 */

#pragma once

namespace ble_gate {

/** Decide from the persisted fabric table whether BLE is needed this boot.
 *
 * Must run after nvs_flash_init() and before esp_matter::start(). Fails toward
 * BLE on: anything unreadable or unparseable counts as unprovisioned. */
void init(void);

/** True if the BLE controller is being kept off this boot. Once it is, the only
 * way to bring it back is a restart. */
bool suppressed(void);

/** After esp_matter::start(): if BLE was kept off, hand its static memory back
 * to the heap */
void release_memory(void);

}  // namespace ble_gate
