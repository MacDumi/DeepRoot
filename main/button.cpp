#include "button.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "app_config.h"

namespace button {

static const char *TAG = "button";

using namespace app_config::button;

static constexpr uint32_t kTaskStack = 3072;
static constexpr UBaseType_t kTaskPriority = 5;

static TaskHandle_t s_task = nullptr;
static handler_t s_handler = nullptr;

/* The trigger is LEVEL-triggered, not edge. That is forced by the wake path:
 * gpio_wakeup_enable() rejects edge modes outright, and it rewrites the pin's
 * interrupt type to the level it was given. A level trigger re-asserts for as
 * long as the button is down, so the ISR has to mask it and let the task re-arm
 * once the press has been dealt with */
static void IRAM_ATTR isr_handler(void *arg)
{
    gpio_intr_disable(kUserButtonGpio);

    BaseType_t higher_woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_task, &higher_woken);
    portYIELD_FROM_ISR(higher_woken);
}

static void button_task(void *arg)
{
    int short_presses = 0;

    for (;;) {
        /* Mid-sequence, wait only as long as the gap: a timeout means no
         * further press is coming, so the sequence is complete. That is the
         * only point at which a single press can be told from the start of a
         * multi-press. */
        const TickType_t wait =
            short_presses > 0 ? pdMS_TO_TICKS(kMultiPressGapMs) : portMAX_DELAY;
        if (ulTaskNotifyTake(pdTRUE, wait) == 0) {
            if (short_presses == 1) {
                s_handler(gesture::SINGLE_PRESS);
            }
            short_presses = 0;
            continue;
        }

        const TickType_t pressed_at = xTaskGetTickCount();
        bool fired = false;

        /* Poll only for as long as the button is held. A release -- including a
         * bounce long enough to be sampled -- ends the press and re-arms, so a
         * flaky contact fails toward *not* firing the long hold (a factory reset). */
        while (gpio_get_level(kUserButtonGpio) == 0) {
            if (!fired && xTaskGetTickCount() - pressed_at >= pdMS_TO_TICKS(kLongHoldMs)) {
                fired = true;
                s_handler(gesture::LONG_HOLD);
            }

            vTaskDelay(pdMS_TO_TICKS(kButtonPollMs));
        }

        const TickType_t held = xTaskGetTickCount() - pressed_at;
        const bool is_press = !fired && held >= pdMS_TO_TICKS(kButtonDebounceMs);

        /* Bounce shorter than the debounce floor neither counts nor breaks
         * the sequence; a long press does. A pause longer than the gap ends
         * it through the wait timeout above. */
        if (is_press) {
            if (held >= pdMS_TO_TICKS(kShortPressMaxMs)) {
                short_presses = 0;
            } else {
                ++short_presses;
            }
            if (short_presses == kMultiPressCount) {
                short_presses = 0;
                s_handler(gesture::MULTI_PRESS);
            }
        }

        /* Re-arm only now that the line has gone back high. Doing it any
         * earlier would re-enter the ISR immediately. */
        ESP_ERROR_CHECK(gpio_intr_enable(kUserButtonGpio));
    }
}

static esp_err_t configure_gpio(void)
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << kUserButtonGpio;
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    /* Interrupt stays off until the handler is in place: with a level trigger,
     * arming it while the button happens to be held would hand the storm to
     * the default ISR. */
    cfg.intr_type = GPIO_INTR_DISABLE;
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }

    /* Another component may already own the ISR service; */
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = gpio_isr_handler_add(kUserButtonGpio, isr_handler, nullptr);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_set_intr_type(kUserButtonGpio, GPIO_INTR_LOW_LEVEL);
    if (err != ESP_OK) {
        return err;
    }

#ifdef CONFIG_PM_ENABLE
    /* The GPIO peripheral is unclocked in light sleep, so only a level can wake
     * the CPU. Low is the pressed state. */
    err = gpio_wakeup_enable(kUserButtonGpio, GPIO_INTR_LOW_LEVEL);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_sleep_enable_gpio_wakeup();
    if (err != ESP_OK) {
        return err;
    }
#endif

    return gpio_intr_enable(kUserButtonGpio);
}

esp_err_t start(handler_t handler)
{
    if (handler == nullptr || s_task != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    s_handler = handler;

    if (xTaskCreate(button_task, "button", kTaskStack, nullptr, kTaskPriority, &s_task) !=
        pdPASS) {
        s_task = nullptr;
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = configure_gpio();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "button GPIO setup failed: %s", esp_err_to_name(err));
        gpio_intr_disable(kUserButtonGpio);
        gpio_isr_handler_remove(kUserButtonGpio);
        vTaskDelete(s_task);
        s_task = nullptr;
    }
    return err;
}

}  // namespace button
