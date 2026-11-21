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

static void reduce_reset_result(app_state_t *state, bool success,
                                int64_t now_us)
{
    const app_event_t event = {
        .type = APP_EVENT_BOND_RESET_RESULT,
        .data.bond_reset_succeeded = success,
    };
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
    TEST_CHECK(state.bond_reset_progress_visible);
    TEST_CHECK(state.bond_reset_state == APP_BOND_RESET_HOLDING);
    reduce_type(&state, APP_EVENT_TIMER,
                100U + APP_BUTTON_B_LONG_HOLD_US - 1U);
    TEST_CHECK(!state.forget_bond_requested);

    reduce_type(&state, APP_EVENT_TIMER,
                100U + APP_BUTTON_B_LONG_HOLD_US);
    TEST_CHECK(state.forget_bond_requested);
    TEST_CHECK(state.bond_reset_state == APP_BOND_RESET_REQUESTED);
    TEST_CHECK(app_state_take_forget_bond_request(&state));
    TEST_CHECK(state.bond_reset_state == APP_BOND_RESET_SUBMITTED);
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

static void test_b_hold_progress_milestones(void)
{
    app_state_t state;
    app_state_init(&state);

    reduce_type(&state, APP_EVENT_BUTTON_B_DOWN, 100U);
    TEST_CHECK(state.bond_reset_progress_percent == 0U);
    reduce_type(&state, APP_EVENT_TIMER, 100U + 1000000U);
    TEST_CHECK(state.bond_reset_progress_percent == 20U);
    reduce_type(&state, APP_EVENT_TIMER, 100U + 2500000U);
    TEST_CHECK(state.bond_reset_progress_percent == 50U);
    reduce_type(&state, APP_EVENT_TIMER,
                100U + APP_BUTTON_B_LONG_HOLD_US - 1U);
    TEST_CHECK(state.bond_reset_progress_percent == 99U);
    TEST_CHECK(!state.forget_bond_requested);
    reduce_type(&state, APP_EVENT_TIMER,
                100U + APP_BUTTON_B_LONG_HOLD_US);
    TEST_CHECK(state.bond_reset_progress_percent == 100U);
    TEST_CHECK(app_state_take_forget_bond_request(&state));

    reduce_type(&state, APP_EVENT_TIMER,
                100U + APP_BUTTON_B_LONG_HOLD_US + 1000000U);
    TEST_CHECK(state.bond_reset_progress_percent == 100U);
    TEST_CHECK(!app_state_take_forget_bond_request(&state));
}

static void test_early_b_release_resets_progress_and_navigates_once(void)
{
    app_state_t state;
    app_state_init(&state);
    for (uint32_t uid = 1U; uid <= 3U; ++uid) {
        app_event_t event = notification_event(
            notification(uid, TELEGRAM_IOS_BUNDLE_ID, "message"));
        app_state_reduce(&state, &event, (int64_t)uid);
    }
    reduce_type(&state, APP_EVENT_BUTTON_A, 10U);
    TEST_CHECK(notification_queue_current(&state.queue)->uid == 2U);

    reduce_type(&state, APP_EVENT_BUTTON_B_DOWN, 100U);
    reduce_type(&state, APP_EVENT_TIMER, 2000100U);
    TEST_CHECK(state.bond_reset_progress_percent == 40U);
    reduce_type(&state, APP_EVENT_BUTTON_B_UP, 2000101U);
    TEST_CHECK(!state.bond_reset_progress_visible);
    TEST_CHECK(state.bond_reset_progress_percent == 0U);
    TEST_CHECK(state.bond_reset_state == APP_BOND_RESET_IDLE);
    TEST_CHECK(notification_queue_current(&state.queue)->uid == 3U);

    reduce_type(&state, APP_EVENT_BUTTON_B_UP, 2000102U);
    TEST_CHECK(notification_queue_current(&state.queue)->uid == 3U);
    TEST_CHECK(!state.forget_bond_requested);
}

static void test_reset_success_completes_only_after_result_until_pairing(void)
{
    app_state_t state;
    app_state_init(&state);

    reduce_type(&state, APP_EVENT_BUTTON_B_DOWN, 100U);
    reduce_type(&state, APP_EVENT_BUTTON_B_UP,
                100U + APP_BUTTON_B_LONG_HOLD_US);
    TEST_CHECK(!state.button_b_is_down);
    TEST_CHECK(state.bond_reset_progress_visible);
    TEST_CHECK(state.bond_reset_progress_percent == 100U);
    TEST_CHECK(state.bond_reset_state == APP_BOND_RESET_REQUESTED);
    TEST_CHECK(app_state_take_forget_bond_request(&state));
    TEST_CHECK(state.bond_reset_state == APP_BOND_RESET_SUBMITTED);

    reduce_reset_result(&state, true,
                        100U + APP_BUTTON_B_LONG_HOLD_US + 1U);
    TEST_CHECK(state.bond_reset_state == APP_BOND_RESET_COMPLETED);
    TEST_CHECK(state.bond_reset_progress_visible);

    reduce_type(&state, APP_EVENT_BUTTON_B_UP,
                100U + APP_BUTTON_B_LONG_HOLD_US + 1U);
    TEST_CHECK(!app_state_take_forget_bond_request(&state));
    TEST_CHECK(state.bond_reset_progress_visible);

    const app_event_t pairing = {
        .type = APP_EVENT_CONNECTION_STATE,
        .data.connection = APP_CONNECTION_PAIRING,
    };
    app_state_reduce(&state, &pairing,
                     100U + APP_BUTTON_B_LONG_HOLD_US + 2U);
    TEST_CHECK(!state.bond_reset_progress_visible);
    TEST_CHECK(state.bond_reset_progress_percent == 0U);
    TEST_CHECK(state.bond_reset_state == APP_BOND_RESET_IDLE);
}

static void test_reset_failure_is_bounded_and_can_be_requested_again(void)
{
    app_state_t state;
    app_state_init(&state);

    reduce_type(&state, APP_EVENT_BUTTON_B_DOWN, 10U);
    reduce_type(&state, APP_EVENT_TIMER,
                10U + APP_BUTTON_B_LONG_HOLD_US);
    TEST_CHECK(app_state_take_forget_bond_request(&state));
    reduce_reset_result(&state, false,
                        10U + APP_BUTTON_B_LONG_HOLD_US + 1U);
    TEST_CHECK(state.bond_reset_state == APP_BOND_RESET_FAILED);
    TEST_CHECK(state.bond_reset_progress_visible);
    TEST_CHECK(state.bond_reset_progress_percent == 0U);

    reduce_type(&state, APP_EVENT_TIMER,
                10U + APP_BUTTON_B_LONG_HOLD_US +
                    APP_BOND_RESET_FEEDBACK_US);
    TEST_CHECK(state.bond_reset_state == APP_BOND_RESET_FAILED);
    reduce_type(&state, APP_EVENT_TIMER,
                10U + APP_BUTTON_B_LONG_HOLD_US + 1U +
                    APP_BOND_RESET_FEEDBACK_US);
    TEST_CHECK(state.bond_reset_state == APP_BOND_RESET_IDLE);
    TEST_CHECK(!state.bond_reset_progress_visible);

    reduce_type(&state, APP_EVENT_BUTTON_B_UP,
                10U + APP_BUTTON_B_LONG_HOLD_US + 2U +
                    APP_BOND_RESET_FEEDBACK_US);
    reduce_type(&state, APP_EVENT_BUTTON_B_DOWN, 10000000U);
    reduce_type(&state, APP_EVENT_TIMER,
                10000000U + APP_BUTTON_B_LONG_HOLD_US);
    TEST_CHECK(app_state_take_forget_bond_request(&state));
    TEST_CHECK(!app_state_take_forget_bond_request(&state));
}

static void test_display_failure_clears_visible_assumption(void)
{
    app_state_t state;
    app_state_init(&state);
    reduce_type(&state, APP_EVENT_BUTTON_A, 100U);
    TEST_CHECK(state.display_awake);

    reduce_type(&state, APP_EVENT_DISPLAY_FAILURE, 101U);
    TEST_CHECK(!state.display_awake);
    TEST_CHECK(state.display_deadline_us == 0U);
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
    test_b_hold_progress_milestones();
    test_early_b_release_resets_progress_and_navigates_once();
    test_reset_success_completes_only_after_result_until_pairing();
    test_reset_failure_is_bounded_and_can_be_requested_again();
    test_display_failure_clears_visible_assumption();
    test_display_deadline_and_delayed_processing();
    test_connection_state();
    test_battery_voltage_mapping();
    return 0;
}
