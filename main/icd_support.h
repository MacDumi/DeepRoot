/*
 * ICD (Intermittently Connected Device) glue: interval configuration, the
 * User Active Mode Trigger, and Check-In on subscription loss.
 *
 * Every function is a no-op on a build without CONFIG_ENABLE_ICD_SERVER, so
 * callers do not need their own #ifdefs.
 */

#pragma once

#include <esp_err.h>
#include <esp_matter.h>

namespace icd_support {

/** Hand the five ICD intervals in app_config.h to esp_matter.
 *
 * Must run before node::create() -- set_configuration_data() refuses to touch
 * enable_icd_server once the data model exists, and refuses everything else
 * once esp_matter::start() has run. */
esp_err_t configure(void);

/** Describe the User Active Mode Trigger in the root node config, so the ICD
 * Management cluster tells a controller which control to use. Before
 * node::create(). */
void describe_user_active_trigger(esp_matter::node::config_t &config);

/** After esp_matter::start(): arm Check-In on subscription loss. */
void start(void);

/** User Active Mode Trigger. Safe from any task; the work runs on the Matter
 * task. */
void notify_user_active(void);

}  // namespace icd_support
