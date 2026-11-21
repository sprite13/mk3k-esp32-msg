#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "notification.h"

typedef enum { UI_PAIRING, UI_CONNECTED, UI_RECONNECTING, UI_ERROR } ui_connection_state_t;

typedef struct {
    ui_connection_state_t connection;
    uint8_t battery_percent;
    size_t position;
    size_t total;
    const notification_t *notification;
} display_ui_model_t;

esp_err_t display_ui_init(void);
void display_ui_render(const display_ui_model_t *model);
void display_ui_sleep(void);
