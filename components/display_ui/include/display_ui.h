#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "notification.h"

typedef enum { UI_PAIRING, UI_CONNECTED, UI_RECONNECTING, UI_ERROR } ui_connection_state_t;

typedef enum {
    UI_BOND_RESET_NONE,
    UI_BOND_RESET_HOLDING,
    UI_BOND_RESET_REQUESTED,
    UI_BOND_RESET_COMPLETED,
    UI_BOND_RESET_FAILED,
} ui_bond_reset_state_t;

typedef struct {
    ui_connection_state_t connection;
    uint8_t battery_percent; /* 0..100; values above 100 mean unknown. */
    size_t position;
    size_t total;
    const notification_t *notification;
    ui_bond_reset_state_t bond_reset_state;
    uint8_t bond_reset_progress_percent; /* Clamped to 0..100 by layout. */
} display_ui_model_t;

esp_err_t display_ui_init(void);
void display_ui_render(const display_ui_model_t *model);
/* True only while the initialized panel is awake and usable. */
bool display_ui_is_healthy(void);
void display_ui_sleep(void);
