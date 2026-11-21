#include <stdint.h>
#include <string.h>

#include "app_screen.h"
#include "notification_normalizer.h"
#include "test_check.h"

static void reduce_type(app_state_t *state, app_event_type_t type,
                        int64_t now_us)
{
    const app_event_t event = {.type = type};
    app_state_reduce(state, &event, now_us);
}

static void test_default_and_battery_screen(void)
{
    app_state_t state;
    app_state_init(&state);
    app_state_reduce(
        &state,
        &(app_event_t){.type = APP_EVENT_BATTERY_PERCENT,
                       .data.battery_percent = 14U},
        0);

    app_screen_t screen;
    app_screen_make(&state, &screen);
    TEST_CHECK(screen.battery_percent == 14U);
    TEST_CHECK(screen.connection == APP_CONNECTION_PAIRING);
    TEST_CHECK(screen.total == 0U);
    TEST_CHECK(!screen.notification_present);
    TEST_CHECK(!screen.display_awake);
}

static void test_screen_copies_selection_and_tracks_state(void)
{
    app_state_t state;
    app_state_init(&state);
    app_state_reduce(
        &state,
        &(app_event_t){.type = APP_EVENT_CONNECTION_STATE,
                       .data.connection = APP_CONNECTION_CONNECTED},
        0);
    for (uint32_t uid = 1U; uid <= 2U; ++uid) {
        notification_t item = {.uid = uid};
        strncpy(item.app_id, TELEGRAM_IOS_BUNDLE_ID,
                sizeof(item.app_id) - 1U);
        const app_event_t event = {
            .type = APP_EVENT_NOTIFICATION,
            .data.notification = item,
        };
        app_state_reduce(&state, &event, (int64_t)uid);
    }
    reduce_type(&state, APP_EVENT_BUTTON_A, 10U);
    reduce_type(&state, APP_EVENT_BUTTON_B_DOWN, 20U);
    reduce_type(&state, APP_EVENT_TIMER, 2500020U);

    app_screen_t screen;
    app_screen_make(&state, &screen);
    TEST_CHECK(screen.notification_present);
    TEST_CHECK(screen.connection == APP_CONNECTION_CONNECTED);
    TEST_CHECK(screen.notification.uid == 1U);
    TEST_CHECK(screen.position == 2U);
    TEST_CHECK(screen.total == 2U);
    TEST_CHECK(screen.bond_reset_state == APP_BOND_RESET_HOLDING);
    TEST_CHECK(screen.bond_reset_progress_percent == 50U);
    TEST_CHECK(screen.display_awake);

    const notification_t *const selected =
        notification_queue_current(&state.queue);
    const size_t selected_index = (size_t)(selected - state.queue.items);
    state.queue.items[selected_index].uid = 99U;
    TEST_CHECK(screen.notification.uid == 1U);
}

static void test_null_inputs_are_defensive(void)
{
    app_screen_t screen = {
        .notification_present = true,
        .display_awake = true,
    };
    app_screen_make(NULL, &screen);
    TEST_CHECK(!screen.notification_present);
    TEST_CHECK(!screen.display_awake);
    app_screen_make(NULL, NULL);

    app_state_t state;
    app_state_init(&state);
    app_screen_make(&state, NULL);
}

int main(void)
{
    test_default_and_battery_screen();
    test_screen_copies_selection_and_tracks_state();
    test_null_inputs_are_defensive();
    return 0;
}
