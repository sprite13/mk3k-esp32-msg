#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "simulator_api.h"
#include "test_check.h"

static simulator_snapshot_t snapshot(void)
{
    simulator_snapshot_t result;
    TEST_CHECK(simulator_snapshot(&result) == SIM_OK);
    return result;
}

static void connect_at(int64_t now_us)
{
    TEST_CHECK(simulator_set_connection(SIM_CONNECTION_CONNECTED, now_us) ==
               SIM_OK);
}

static void test_reset_version_and_defaults(void)
{
    TEST_CHECK(simulator_reset(0) == SIM_OK);
    TEST_CHECK(simulator_abi_version() == SIMULATOR_ABI_VERSION);

    const simulator_snapshot_t state = snapshot();
    TEST_CHECK(state.abi_version == SIMULATOR_ABI_VERSION);
    TEST_CHECK(state.connection == SIM_CONNECTION_PAIRING);
    TEST_CHECK(state.battery_percent == SIM_BATTERY_PERCENT_UNKNOWN);
    TEST_CHECK(state.position == 0U);
    TEST_CHECK(state.total == 0U);
    TEST_CHECK(state.display_awake == 0U);
    TEST_CHECK(state.notification_present == 0U);
    TEST_CHECK(state.beep_requested == 0U);
    TEST_CHECK(state.reserved == 0U);
    TEST_CHECK(state.notification.title[0] == '\0');
}

static void test_connection_battery_and_validation(void)
{
    TEST_CHECK(simulator_reset(100) == SIM_OK);
    TEST_CHECK(simulator_set_connection(SIM_CONNECTION_CONNECTED, 101) ==
               SIM_OK);
    TEST_CHECK(simulator_set_battery(73U, 102) == SIM_OK);
    simulator_snapshot_t state = snapshot();
    TEST_CHECK(state.connection == SIM_CONNECTION_CONNECTED);
    TEST_CHECK(state.battery_percent == 73U);

    TEST_CHECK(simulator_snapshot(NULL) == SIM_ERR_NULL_ARGUMENT);
    TEST_CHECK(simulator_set_connection(99U, 103) ==
               SIM_ERR_INVALID_ARGUMENT);
    TEST_CHECK(simulator_set_battery(101U, 103) == SIM_ERR_INVALID_ARGUMENT);
    TEST_CHECK(simulator_tick(99) == SIM_ERR_INVALID_TIMESTAMP);
    TEST_CHECK(simulator_tick(-1) == SIM_ERR_INVALID_TIMESTAMP);
    TEST_CHECK(simulator_button_a(INT64_MAX) == SIM_ERR_INVALID_TIMESTAMP);
    state = snapshot();
    TEST_CHECK(state.connection == SIM_CONNECTION_CONNECTED);
    TEST_CHECK(state.battery_percent == 73U);
}

static void test_notification_normalization_and_beep(void)
{
    TEST_CHECK(simulator_reset(0) == SIM_OK);
    connect_at(10);
    TEST_CHECK(simulator_send_notification("  Алиса  ", " канал ",
                                               "Привет 👋  мир", 20) ==
               SIM_OK);
    simulator_snapshot_t state = snapshot();
    TEST_CHECK(state.total == 1U);
    TEST_CHECK(state.position == 1U);
    TEST_CHECK(state.display_awake == 1U);
    TEST_CHECK(state.notification_present == 1U);
    TEST_CHECK(strcmp(state.notification.app_id, "ph.telegra.Telegraph") == 0);
    TEST_CHECK(strcmp(state.notification.title, "Алиса") == 0);
    TEST_CHECK(strcmp(state.notification.subtitle, "канал") == 0);
    TEST_CHECK(strcmp(state.notification.message, "Привет мир") == 0);
    TEST_CHECK(state.beep_requested == 1U);
    TEST_CHECK(simulator_take_beep() == 1U);
    TEST_CHECK(simulator_take_beep() == 0U);
    TEST_CHECK(snapshot().beep_requested == 0U);

    TEST_CHECK(simulator_send_notification(NULL, "", "", 21) ==
               SIM_ERR_NULL_ARGUMENT);
    TEST_CHECK(simulator_send_notification("", NULL, "", 21) ==
               SIM_ERR_NULL_ARGUMENT);
    TEST_CHECK(simulator_send_notification("", "", NULL, 21) ==
               SIM_ERR_NULL_ARGUMENT);
}

static void test_overlong_utf8_is_safely_truncated(void)
{
    char long_text[600];
    size_t offset = 0U;
    while (offset + 2U < sizeof(long_text)) {
        long_text[offset++] = (char)0xD0;
        long_text[offset++] = (char)0xAF;
    }
    long_text[offset] = '\0';

    TEST_CHECK(simulator_reset(0) == SIM_OK);
    connect_at(1);
    TEST_CHECK(simulator_send_notification(long_text, long_text, long_text, 2) ==
               SIM_OK);
    const simulator_snapshot_t state = snapshot();
    TEST_CHECK(state.notification.title[NOTIFICATION_TITLE_MAX - 1U] == '\0');
    TEST_CHECK(state.notification.subtitle[NOTIFICATION_SUBTITLE_MAX - 1U] ==
               '\0');
    TEST_CHECK(state.notification.message[NOTIFICATION_MESSAGE_MAX - 1U] ==
               '\0');
    TEST_CHECK(strlen(state.notification.title) == 94U);
    TEST_CHECK(strlen(state.notification.message) == 254U);
}

static void test_disconnected_and_hidden_previews(void)
{
    TEST_CHECK(simulator_reset(0) == SIM_OK);
    TEST_CHECK(simulator_send_notification("name", "chat", "text", 1) ==
               SIM_ERR_DISCONNECTED);
    TEST_CHECK(snapshot().total == 0U);
    TEST_CHECK(simulator_take_beep() == 0U);

    connect_at(2);
    TEST_CHECK(simulator_set_previews_hidden(true) == SIM_OK);
    TEST_CHECK(simulator_send_notification("name", "chat", "text", 3) ==
               SIM_OK);
    const simulator_snapshot_t state = snapshot();
    TEST_CHECK(state.notification.title[0] == '\0');
    TEST_CHECK(state.notification.subtitle[0] == '\0');
    TEST_CHECK(state.notification.message[0] == '\0');
    TEST_CHECK(simulator_take_beep() == 1U);
}

static void test_eviction_and_button_ordering(void)
{
    TEST_CHECK(simulator_reset(0) == SIM_OK);
    connect_at(1);
    for (int item = 1; item <= 6; ++item) {
        char title[2] = {(char)('0' + item), '\0'};
        TEST_CHECK(simulator_send_notification(title, "", "", item + 1) ==
                   SIM_OK);
    }
    simulator_snapshot_t state = snapshot();
    TEST_CHECK(state.total == 5U);
    TEST_CHECK(state.position == 1U);
    TEST_CHECK(strcmp(state.notification.title, "6") == 0);

    TEST_CHECK(simulator_button_a(10) == SIM_OK);
    state = snapshot();
    TEST_CHECK(state.position == 2U);
    TEST_CHECK(strcmp(state.notification.title, "5") == 0);
    for (int press = 0; press < 8; ++press) {
        TEST_CHECK(simulator_button_a(11 + press) == SIM_OK);
    }
    state = snapshot();
    TEST_CHECK(state.position == 5U);
    TEST_CHECK(strcmp(state.notification.title, "2") == 0);

    TEST_CHECK(simulator_button_b_down(20) == SIM_OK);
    TEST_CHECK(simulator_button_b_up(21) == SIM_OK);
    state = snapshot();
    TEST_CHECK(state.position == 4U);
    TEST_CHECK(strcmp(state.notification.title, "3") == 0);
}

static void test_timeout_and_long_hold(void)
{
    TEST_CHECK(simulator_reset(0) == SIM_OK);
    connect_at(10);
    TEST_CHECK(simulator_send_notification("A", "", "B", 20) == SIM_OK);
    TEST_CHECK(simulator_send_notification("C", "", "D", 21) == SIM_OK);
    TEST_CHECK(simulator_button_a(22) == SIM_OK);
    TEST_CHECK(snapshot().position == 2U);
    TEST_CHECK(simulator_tick(INT64_C(10000021)) == SIM_OK);
    TEST_CHECK(snapshot().display_awake == 1U);
    TEST_CHECK(simulator_tick(INT64_C(10000022)) == SIM_OK);
    TEST_CHECK(snapshot().display_awake == 0U);

    TEST_CHECK(simulator_button_b_down(INT64_C(10000023)) == SIM_OK);
    simulator_snapshot_t state = snapshot();
    TEST_CHECK(state.display_awake == 1U);
    TEST_CHECK(state.position == 2U);
    TEST_CHECK(simulator_tick(INT64_C(12500023)) == SIM_OK);
    state = snapshot();
    TEST_CHECK(state.position == 2U);
    TEST_CHECK(simulator_tick(INT64_C(15000023)) == SIM_OK);
    TEST_CHECK(simulator_button_b_up(INT64_C(15000024)) == SIM_OK);
    state = snapshot();
    TEST_CHECK(state.position == 2U);
    TEST_CHECK(state.display_awake == 1U);
}

int main(void)
{
    test_reset_version_and_defaults();
    test_connection_battery_and_validation();
    test_notification_normalization_and_beep();
    test_overlong_utf8_is_safely_truncated();
    test_disconnected_and_hidden_previews();
    test_eviction_and_button_ordering();
    test_timeout_and_long_hold();
    return 0;
}
