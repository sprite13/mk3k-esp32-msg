#include "app_screen.h"

#include <string.h>

void app_screen_make(const app_state_t *state, app_screen_t *screen)
{
    if (screen == NULL) {
        return;
    }

    memset(screen, 0, sizeof(*screen));
    screen->battery_percent = APP_BATTERY_PERCENT_UNKNOWN;
    if (state == NULL) {
        return;
    }

    screen->connection = state->connection;
    screen->battery_percent = state->battery_percent;
    screen->total = notification_queue_size(&state->queue);
    screen->position =
        screen->total == 0U ? 0U : state->queue.selected_offset + 1U;
    const notification_t *const selected =
        notification_queue_current(&state->queue);
    if (selected != NULL) {
        screen->notification = *selected;
        screen->notification_present = true;
    }
    screen->bond_reset_state = state->bond_reset_state;
    screen->bond_reset_progress_percent =
        state->bond_reset_progress_percent;
    screen->display_awake = state->display_awake;
}
