#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "notification.h"
#include "notification_queue.h"

#define APP_DISPLAY_TIMEOUT_US INT64_C(10000000)
#define APP_BUTTON_B_LONG_HOLD_US INT64_C(5000000)
#define APP_BOND_RESET_FEEDBACK_US INT64_C(3000000)
#define APP_BATTERY_PERCENT_UNKNOWN UINT8_MAX

typedef enum {
    APP_EVENT_NOTIFICATION,
    /* A is emitted once on its press edge and navigates older immediately. */
    APP_EVENT_BUTTON_A,
    /*
     * B_DOWN starts one hold. B_UP before five seconds navigates newer;
     * reaching five seconds requests bond reset once and suppresses navigation.
     */
    APP_EVENT_BUTTON_B_DOWN,
    APP_EVENT_BUTTON_B_UP,
    /* Evaluates monotonic display and B-hold deadlines. */
    APP_EVENT_TIMER,
    APP_EVENT_CONNECTION_STATE,
    APP_EVENT_BOND_RESET_RESULT,
    APP_EVENT_DISPLAY_FAILURE,
    APP_EVENT_BATTERY_PERCENT,
} app_event_type_t;

typedef enum {
    APP_CONNECTION_PAIRING,
    APP_CONNECTION_CONNECTED,
    APP_CONNECTION_RECONNECTING,
    APP_CONNECTION_ERROR,
} app_connection_state_t;

typedef enum {
    APP_BOND_RESET_IDLE,
    APP_BOND_RESET_HOLDING,
    APP_BOND_RESET_REQUESTED,
    APP_BOND_RESET_SUBMITTED,
    APP_BOND_RESET_COMPLETED,
    APP_BOND_RESET_FAILED,
} app_bond_reset_state_t;

typedef struct {
    app_event_type_t type;
    union {
        notification_t notification;
        app_connection_state_t connection;
        bool bond_reset_succeeded;
        uint8_t battery_percent;
    } data;
} app_event_t;

typedef struct {
    notification_queue_t queue;
    app_connection_state_t connection;
    uint8_t battery_percent;
    bool display_awake;
    bool forget_bond_requested;
    bool notification_accepted;
    bool button_b_is_down;
    bool button_b_long_handled;
    app_bond_reset_state_t bond_reset_state;
    bool bond_reset_progress_visible;
    uint8_t bond_reset_progress_percent;
    int64_t display_deadline_us;
    int64_t button_b_down_us;
    int64_t bond_reset_feedback_deadline_us;
} app_state_t;

void app_state_init(app_state_t *state);
void app_state_reduce(app_state_t *state, const app_event_t *event,
                      int64_t now_us);
bool app_state_take_forget_bond_request(app_state_t *state);
