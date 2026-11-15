/*
 * Shared ADC1 access, opened per reading
 *
 */

#pragma once

#include <esp_adc/adc_oneshot.h>
#include <esp_err.h>

namespace adc_bus {

/** Create the lock. Call once, before any open(). */
esp_err_t init(void);

/** Bring the ADC up with one channel configured, and take exclusive use of it.
 *
 * Blocks while another reader holds the bus. Every successful open() must be
 * matched by close(). On failure the bus is not held.
 *
 * @param channel  ADC1 channel to configure
 * @param atten    attenuation for that channel
 * @param[out] out the unit handle, valid until close()
 */
esp_err_t open(adc_channel_t channel, adc_atten_t atten, adc_oneshot_unit_handle_t *out);

/** Power the ADC down and give up the bus. */
void close(void);

}  // namespace adc_bus
