#include "app_state.h"

#include <stddef.h>

#include "notification_normalizer.h"

static void wake_display(app_state_t *state, int64_t now_us)
{
    state->display_awake = true;
    state->display_deadline_us = now_us + APP_DISPLAY_TIMEOUT_US;
}

static void evaluate_deadlines(app_state_t *state, int64_t now_us)
{
    if (state->display_awake && now_us >= state->display_deadline_us) {
        state->display_awake = false;
    }

    if ((state->bond_reset_state == APP_BOND_RESET_COMPLETED ||
         state->bond_reset_state == APP_BOND_RESET_FAILED) &&
        now_us >= state->bond_reset_feedback_deadline_us) {
        state->bond_reset_state = APP_BOND_RESET_IDLE;
        state->bond_reset_progress_visible = false;
        state->bond_reset_feedback_deadline_us = 0;
    }

    if (state->button_b_is_down && !state->button_b_long_handled &&
        now_us >= state->button_b_down_us) {
        const int64_t elapsed_us = now_us - state->button_b_down_us;
        if (elapsed_us >= APP_BUTTON_B_LONG_HOLD_US) {
            state->bond_reset_progress_percent = 100U;
            state->button_b_long_handled = true;
            state->forget_bond_requested = true;
            state->bond_reset_state = APP_BOND_RESET_REQUESTED;
            wake_display(state, now_us);
        } else {
            state->bond_reset_progress_percent = (uint8_t)(
                (elapsed_us * 100) / APP_BUTTON_B_LONG_HOLD_US);
        }
    }
}

void app_state_init(app_state_t *state)
{
    notification_queue_init(&state->queue);
    state->connection = APP_CONNECTION_PAIRING;
    state->battery_percent = APP_BATTERY_PERCENT_UNKNOWN;
    state->display_awake = false;
    state->forget_bond_requested = false;
    state->notification_accepted = false;
    state->button_b_is_down = false;
    state->button_b_long_handled = false;
    state->bond_reset_state = APP_BOND_RESET_IDLE;
    state->bond_reset_progress_visible = false;
    state->bond_reset_progress_percent = 0U;
    state->display_deadline_us = 0;
    state->button_b_down_us = 0;
    state->bond_reset_feedback_deadline_us = 0;
}

void app_state_reduce(app_state_t *state, const app_event_t *event,
                      int64_t now_us)
{
    if (state == NULL || event == NULL) {
        return;
    }

    state->notification_accepted = false;
    evaluate_deadlines(state, now_us);

    switch (event->type) {
    case APP_EVENT_NOTIFICATION: {
        notification_t item = event->data.notification;
        if (!notification_is_telegram(&item)) {
            break;
        }
        notification_normalize(&item);
        notification_queue_push(&state->queue, &item);
        state->notification_accepted = true;
        wake_display(state, now_us);
        break;
    }
    case APP_EVENT_BUTTON_A:
        (void)notification_queue_older(&state->queue);
        wake_display(state, now_us);
        break;
    case APP_EVENT_BUTTON_B_DOWN:
        if (!state->button_b_is_down) {
            state->button_b_is_down = true;
            state->button_b_long_handled = false;
            state->bond_reset_state = APP_BOND_RESET_HOLDING;
            state->bond_reset_progress_visible = true;
            state->bond_reset_progress_percent = 0U;
            state->bond_reset_feedback_deadline_us = 0;
            state->button_b_down_us = now_us;
            wake_display(state, now_us);
        }
        break;
    case APP_EVENT_BUTTON_B_UP:
        if (state->button_b_is_down && !state->button_b_long_handled) {
            (void)notification_queue_newer(&state->queue);
            state->bond_reset_progress_visible = false;
            state->bond_reset_progress_percent = 0U;
            state->bond_reset_state = APP_BOND_RESET_IDLE;
            state->bond_reset_feedback_deadline_us = 0;
            wake_display(state, now_us);
        }
        state->button_b_is_down = false;
        state->button_b_long_handled = false;
        state->button_b_down_us = 0;
        break;
    case APP_EVENT_TIMER:
        break;
    case APP_EVENT_CONNECTION_STATE:
        if (event->data.connection >= APP_CONNECTION_PAIRING &&
            event->data.connection <= APP_CONNECTION_ERROR) {
            state->connection = event->data.connection;
            if (state->connection == APP_CONNECTION_PAIRING &&
                state->bond_reset_state == APP_BOND_RESET_COMPLETED) {
                state->bond_reset_progress_visible = false;
                state->bond_reset_progress_percent = 0U;
                state->bond_reset_state = APP_BOND_RESET_IDLE;
                state->bond_reset_feedback_deadline_us = 0;
            }
            wake_display(state, now_us);
        }
        break;
    case APP_EVENT_BOND_RESET_RESULT:
        if (state->bond_reset_state == APP_BOND_RESET_SUBMITTED) {
            state->bond_reset_state = event->data.bond_reset_succeeded ?
                                          APP_BOND_RESET_COMPLETED :
                                          APP_BOND_RESET_FAILED;
            state->bond_reset_progress_visible = true;
            if (event->data.bond_reset_succeeded) {
                state->bond_reset_feedback_deadline_us =
                    now_us + APP_BOND_RESET_FEEDBACK_US;
            } else {
                state->bond_reset_progress_percent = 0U;
                state->bond_reset_feedback_deadline_us =
                    now_us + APP_BOND_RESET_FEEDBACK_US;
            }
            wake_display(state, now_us);
        }
        break;
    case APP_EVENT_DISPLAY_FAILURE:
        state->display_awake = false;
        state->display_deadline_us = 0;
        break;
    case APP_EVENT_BATTERY_PERCENT:
        state->battery_percent = event->data.battery_percent;
        break;
    }
}

bool app_state_take_forget_bond_request(app_state_t *state)
{
    if (state == NULL || !state->forget_bond_requested) {
        return false;
    }
    state->forget_bond_requested = false;
    state->bond_reset_state = APP_BOND_RESET_SUBMITTED;
    return true;
}
