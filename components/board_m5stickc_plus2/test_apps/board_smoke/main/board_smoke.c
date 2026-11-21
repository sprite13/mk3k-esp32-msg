#include <inttypes.h>

#include "board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *const TAG = "board_smoke";

#define BUTTON_POLL_INTERVAL_MS 20U
#define LOG_INTERVAL_MS 500U

void app_main(void)
{
    ESP_ERROR_CHECK(board_power_hold_init());
    board_init();
    board_backlight_set(true);
    board_beep(1000U, 80U);

    TickType_t last_log_tick = xTaskGetTickCount() - pdMS_TO_TICKS(LOG_INTERVAL_MS);
    board_buttons_t previous_buttons = board_buttons_read();
    while (true) {
        const board_buttons_t buttons = board_buttons_read();
        if (buttons.a_down != previous_buttons.a_down ||
            buttons.b_down != previous_buttons.b_down ||
            buttons.power_down != previous_buttons.power_down) {
            ESP_LOGI(TAG, "button change: A=%d B=%d power=%d", buttons.a_down,
                     buttons.b_down, buttons.power_down);
            previous_buttons = buttons;
        }

        const TickType_t now_tick = xTaskGetTickCount();
        if ((now_tick - last_log_tick) >= pdMS_TO_TICKS(LOG_INTERVAL_MS)) {
            const uint32_t battery_mv = board_battery_millivolts();
            ESP_LOGI(TAG, "buttons: A=%d B=%d power=%d; battery=%" PRIu32 " mV",
                     buttons.a_down, buttons.b_down, buttons.power_down, battery_mv);
            last_log_tick = now_tick;
        }
        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_INTERVAL_MS));
    }
}
