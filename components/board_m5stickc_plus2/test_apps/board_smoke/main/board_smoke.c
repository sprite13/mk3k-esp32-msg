#include <inttypes.h>

#include "board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *const TAG = "board_smoke";

void app_main(void)
{
    board_init();
    board_backlight_set(true);
    board_beep(1000U, 80U);

    while (true) {
        const board_buttons_t buttons = board_buttons_read();
        const uint32_t battery_mv = board_battery_millivolts();
        ESP_LOGI(TAG, "buttons: A=%d B=%d power=%d; battery=%" PRIu32 " mV",
                 buttons.a_down, buttons.b_down, buttons.power_down, battery_mv);
        vTaskDelay(pdMS_TO_TICKS(500U));
    }
}
