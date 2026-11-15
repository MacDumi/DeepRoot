#pragma once

#include <esp_err.h>
#include <stdint.h>


namespace led {

enum class color : uint8_t {
    OFF,          // 0
    RED,          // 1
    GREEN,        // 2
    YELLOW,       // 3
    COLOR_CNT,
};

/**
 * Initialize the led;
 */
esp_err_t init();

/**
 * Set the color of the LED
 *
 * @param cl Color to set
 */
esp_err_t set_color(color cl);

} // namespace led