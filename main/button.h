/*
 * User button: turns presses into gestures.
 *
 * The module only recognises gestures; what each one does is decided by the
 * handler passed to start(). The handler runs on the button task, so it may
 * block, but presses are not seen while it runs.
 */

#pragma once

#include <esp_err.h>

namespace button {

enum class gesture {
    /** One short press, reported once the multi-press gap has passed with no
     * further press  */
    SINGLE_PRESS,
    /** kMultiPressCount short presses in a row, reported on the last one. */
    MULTI_PRESS,
    /** Held for kLongHoldMs, reported while still held. */
    LONG_HOLD,
};

using handler_t = void (*)(gesture g);

/** Configure the button GPIO and its wake-up, and start the button task. */
esp_err_t start(handler_t handler);

}  // namespace button
