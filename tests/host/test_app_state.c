#include <stdint.h>
#include <string.h>

#include "app_state.h"
#include "notification_normalizer.h"
#include "test_check.h"

static notification_t notification(uint32_t uid, const char *app_id,
                                   const char *title)
{
    notification_t item = {.uid = uid};
    strncpy(item.app_id, app_id, sizeof(item.app_id) - 1U);
    strncpy(item.title, title, sizeof(item.title) - 1U);
    return item;
}

static app_event_t notification_event(notification_t item)
{
    app_event_t event = {.type = APP_EVENT_NOTIFICATION};
    event.data.notification = item;
    return event;
}

static void reduce_type(app_state_t *state, app_event_type_t type,
                        int64_t now_us)
{
    const app_event_t event = {.type = type};
    app_state_reduce(state, &event, now_us);
}

static void test_filters_and_normalizes_notifications(void)
{
    app_state_t state;
    app_state_init(&state);

    app_event_t event = notification_event(
        notification(1U, "com.example.Other", "ignored"));
    app_state_reduce(&state, &event, 100U);
    TEST_CHECK(notification_queue_size(&state.queue) == 0U);
    TEST_CHECK(!state.notification_accepted);
    TEST_CHECK(!state.display_awake);

    event = notification_event(
        notification(2U, TELEGRAM_IOS_BUNDLE_ID, "  Alice  👋 "));
    app_state_reduce(&state, &event, 200U);
    TEST_CHECK(notification_queue_size(&state.queue) == 1U);
    TEST_CHECK(notification_queue_current(&state.queue)->uid == 2U);
    TEST_CHECK(strcmp(notification_queue_current(&state.queue)->title,
                      "Alice") == 0);
    TEST_CHECK(state.notification_accepted);
    TEST_CHECK(state.display_awake);
    TEST_CHECK(state.display_deadline_us == 200U + APP_DISPLAY_TIMEOUT_US);
}

static void test_newest_selection_and_button_navigation(void)
{
    app_state_t state;
    app_state_init(&state);
    for (uint32_t uid = 1U; uid <= 3U; ++uid) {
        app_event_t event = notification_event(
            notification(uid, TELEGRAM_IOS_BUNDLE_ID, "message"));
        app_state_reduce(&state, &event, (int64_t)uid);
    }
    TEST_CHECK(notification_queue_current(&state.queue)->uid == 3U);

    reduce_type(&state, APP_EVENT_BUTTON_A, 100U);
    TEST_CHECK(notification_queue_current(&state.queue)->uid == 2U);
    TEST_CHECK(state.display_deadline_us == 100U + APP_DISPLAY_TIMEOUT_US);

    reduce_type(&state, APP_EVENT_BUTTON_B_DOWN, 200U);
    reduce_type(&state, APP_EVENT_BUTTON_B_UP,
                200U + APP_BUTTON_B_LONG_HOLD_US - 1U);
    TEST_CHECK(notification_queue_current(&state.queue)->uid == 3U);
    TEST_CHECK(!state.forget_bond_requested);
    TEST_CHECK(state.display_deadline_us ==
               200U + APP_BUTTON_B_LONG_HOLD_US - 1U +
                   APP_DISPLAY_TIMEOUT_US);
}

static void test_long_b_requests_bond_reset_once_per_hold(void)
{
    app_state_t state;
    app_state_init(&state);
    app_event_t event = notification_event(
        notification(1U, TELEGRAM_IOS_BUNDLE_ID, "message"));
    app_state_reduce(&state, &event, 0U);

    reduce_type(&state, APP_EVENT_BUTTON_B_DOWN, 100U);
    reduce_type(&state, APP_EVENT_TIMER,
                100U + APP_BUTTON_B_LONG_HOLD_US - 1U);
    TEST_CHECK(!state.forget_bond_requested);

    reduce_type(&state, APP_EVENT_TIMER,
                100U + APP_BUTTON_B_LONG_HOLD_US);
    TEST_CHECK(state.forget_bond_requested);
    TEST_CHECK(app_state_take_forget_bond_request(&state));
    TEST_CHECK(!app_state_take_forget_bond_request(&state));

    reduce_type(&state, APP_EVENT_TIMER,
                100U + APP_BUTTON_B_LONG_HOLD_US + 1U);
    TEST_CHECK(!state.forget_bond_requested);
    reduce_type(&state, APP_EVENT_BUTTON_B_UP,
                100U + APP_BUTTON_B_LONG_HOLD_US + 2U);
    TEST_CHECK(notification_queue_current(&state.queue)->uid == 1U);
    TEST_CHECK(!state.forget_bond_requested);

    reduce_type(&state, APP_EVENT_BUTTON_B_DOWN, 7000000U);
    reduce_type(&state, APP_EVENT_TIMER,
                7000000U + APP_BUTTON_B_LONG_HOLD_US);
    TEST_CHECK(app_state_take_forget_bond_request(&state));
}

static void test_display_deadline_and_delayed_processing(void)
{
    app_state_t state;
    app_state_init(&state);
    app_event_t event = notification_event(
        notification(1U, TELEGRAM_IOS_BUNDLE_ID, "message"));
    app_state_reduce(&state, &event, 100U);

    reduce_type(&state, APP_EVENT_TIMER,
                100U + APP_DISPLAY_TIMEOUT_US - 1U);
    TEST_CHECK(state.display_awake);
    reduce_type(&state, APP_EVENT_TIMER,
                100U + APP_DISPLAY_TIMEOUT_US + 5000000U);
    TEST_CHECK(!state.display_awake);

    reduce_type(&state, APP_EVENT_BUTTON_A,
                100U + APP_DISPLAY_TIMEOUT_US + 6000000U);
    TEST_CHECK(state.display_awake);
    TEST_CHECK(state.display_deadline_us ==
               100U + APP_DISPLAY_TIMEOUT_US + 6000000U +
                   APP_DISPLAY_TIMEOUT_US);
}

static void test_connection_state(void)
{
    app_state_t state;
    app_state_init(&state);
    TEST_CHECK(state.connection == APP_CONNECTION_PAIRING);

    const app_event_t event = {
        .type = APP_EVENT_CONNECTION_STATE,
        .data.connection = APP_CONNECTION_RECONNECTING,
    };
    app_state_reduce(&state, &event, 50U);
    TEST_CHECK(state.connection == APP_CONNECTION_RECONNECTING);
    TEST_CHECK(state.display_awake);
    TEST_CHECK(state.display_deadline_us == 50U + APP_DISPLAY_TIMEOUT_US);
}

static void test_battery_voltage_mapping(void)
{
    TEST_CHECK(app_battery_percent(0U) == APP_BATTERY_PERCENT_UNKNOWN);
    TEST_CHECK(app_battery_percent(3200U) == 0U);
    TEST_CHECK(app_battery_percent(3300U) == 0U);
    TEST_CHECK(app_battery_percent(3450U) == 10U);
    TEST_CHECK(app_battery_percent(3600U) == 20U);
    TEST_CHECK(app_battery_percent(3650U) == 35U);
    TEST_CHECK(app_battery_percent(3700U) == 50U);
    TEST_CHECK(app_battery_percent(3850U) == 75U);
    TEST_CHECK(app_battery_percent(4200U) == 100U);
    TEST_CHECK(app_battery_percent(4300U) == 100U);
}

int main(void)
{
    test_filters_and_normalizes_notifications();
    test_newest_selection_and_button_navigation();
    test_long_b_requests_bond_reset_once_per_hold();
    test_display_deadline_and_delayed_processing();
    test_connection_state();
    test_battery_voltage_mapping();
    return 0;
}
