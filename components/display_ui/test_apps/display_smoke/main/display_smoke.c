#include "board.h"
#include "display_ui.h"

#include <stddef.h>
#include <string.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static notification_t direct_message(void)
{
    notification_t notification = {0};
    strcpy(notification.title, "Анна");
    strcpy(notification.message, "Привет! Проверка личного сообщения.");
    return notification;
}

static notification_t group_message(void)
{
    notification_t notification = {0};
    strcpy(notification.title, "Команда проекта");
    strcpy(notification.subtitle, "Михаил");
    strcpy(notification.message,
           "Длинный русский текст проверяет перенос по словам, обрезку по границе "
           "экрана и многоточие в последней доступной строке.");
    return notification;
}

void app_main(void)
{
    board_init();
    ESP_ERROR_CHECK(display_ui_init());
    board_backlight_set(true);

    notification_t direct = direct_message();
    notification_t group = group_message();
    const display_ui_model_t screens[] = {
        {.connection = UI_PAIRING, .battery_percent = 83U},
        {.connection = UI_RECONNECTING, .battery_percent = 82U},
        {.connection = UI_ERROR, .battery_percent = 81U},
        {.connection = UI_CONNECTED,
         .battery_percent = 80U,
         .position = 1U,
         .total = 5U,
         .notification = &direct},
        {.connection = UI_CONNECTED,
         .battery_percent = 15U,
         .position = 3U,
         .total = 5U,
         .notification = &group},
    };

    size_t screen = 0U;
    while (true) {
        display_ui_render(&screens[screen]);
        screen = (screen + 1U) % (sizeof(screens) / sizeof(screens[0]));
        vTaskDelay(pdMS_TO_TICKS(3000U));
    }
}
