#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_state.h"

typedef struct {
    app_connection_state_t connection;
    uint8_t battery_percent;
    size_t position;
    size_t total;
    bool notification_present;
    notification_t notification;
    app_bond_reset_state_t bond_reset_state;
    uint8_t bond_reset_progress_percent;
    bool display_awake;
} app_screen_t;

void app_screen_make(const app_state_t *state, app_screen_t *screen);
