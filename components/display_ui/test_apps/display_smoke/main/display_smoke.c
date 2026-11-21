#include "board.h"
#include "display_ui.h"

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const notification_t s_direct = {
    .title = "Анна",
    .message = "Привет! Проверка личного сообщения.",
};

static const notification_t s_group = {
    .title = "Команда проекта",
    .subtitle = "Михаил",
    .message =
        "Длинный русский текст проверяет перенос по словам, обрезку по границе "
        "экрана и многоточие в последней доступной строке.",
};

static const display_ui_model_t s_screens[] = {
    {.connection = UI_PAIRING, .battery_percent = 83U},
    {.connection = UI_RECONNECTING, .battery_percent = 82U},
    {.connection = UI_ERROR, .battery_percent = 81U},
    {.connection = UI_CONNECTED,
     .battery_percent = UINT8_MAX,
     .position = 1U,
     .total = 5U,
     .notification = &s_direct},
    {.connection = UI_CONNECTED,
     .battery_percent = 15U,
     .position = 3U,
     .total = 5U,
     .notification = &s_group},
};

void app_main(void)
{
    board_init();
    ESP_ERROR_CHECK(display_ui_init());
    board_backlight_set(true);

    size_t screen = 0U;
    while (true) {
        display_ui_render(&s_screens[screen]);
        vTaskDelay(pdMS_TO_TICKS(2500U));
        display_ui_sleep();
        vTaskDelay(pdMS_TO_TICKS(500U));
        screen = (screen + 1U) % (sizeof(s_screens) / sizeof(s_screens[0]));
    }
}
