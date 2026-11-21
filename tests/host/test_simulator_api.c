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

static void test_operations_require_reset(void)
{
    simulator_snapshot_t state;
    TEST_CHECK(simulator_snapshot(&state) == SIM_ERR_NOT_INITIALIZED);
    TEST_CHECK(simulator_snapshot(NULL) == SIM_ERR_NOT_INITIALIZED);
    TEST_CHECK(simulator_tick(0) == SIM_ERR_NOT_INITIALIZED);
    TEST_CHECK(simulator_send_notification(NULL, NULL, NULL, 0) ==
               SIM_ERR_NOT_INITIALIZED);
    TEST_CHECK(simulator_button_a(0) == SIM_ERR_NOT_INITIALIZED);
    TEST_CHECK(simulator_button_b_down(0) == SIM_ERR_NOT_INITIALIZED);
    TEST_CHECK(simulator_button_b_up(0) == SIM_ERR_NOT_INITIALIZED);
    TEST_CHECK(simulator_set_connection(SIM_CONNECTION_CONNECTED, 0) ==
               SIM_ERR_NOT_INITIALIZED);
    TEST_CHECK(simulator_set_battery(50U, 0) == SIM_ERR_NOT_INITIALIZED);
    TEST_CHECK(simulator_set_previews_hidden(true) ==
               SIM_ERR_NOT_INITIALIZED);
    TEST_CHECK(simulator_take_beep() == SIM_ERR_NOT_INITIALIZED);
    TEST_CHECK(simulator_display_plan(NULL) == SIM_ERR_NOT_INITIALIZED);
    TEST_CHECK(simulator_font_glyph((uint32_t)'A', NULL) ==
               SIM_ERR_NOT_INITIALIZED);
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
    TEST_CHECK(simulator_set_battery(SIM_BATTERY_PERCENT_UNKNOWN, 103) ==
               SIM_OK);
    TEST_CHECK(snapshot().battery_percent == SIM_BATTERY_PERCENT_UNKNOWN);

    TEST_CHECK(simulator_snapshot(NULL) == SIM_ERR_NULL_ARGUMENT);
    TEST_CHECK(simulator_set_connection(99U, 104) ==
               SIM_ERR_INVALID_ARGUMENT);
    TEST_CHECK(simulator_set_battery(101U, 104) == SIM_ERR_INVALID_ARGUMENT);
    TEST_CHECK(simulator_tick(99) == SIM_ERR_INVALID_TIMESTAMP);
    TEST_CHECK(simulator_tick(-1) == SIM_ERR_INVALID_TIMESTAMP);
    TEST_CHECK(simulator_button_a(INT64_MAX) == SIM_ERR_INVALID_TIMESTAMP);
    state = snapshot();
    TEST_CHECK(state.connection == SIM_CONNECTION_CONNECTED);
    TEST_CHECK(state.battery_percent == SIM_BATTERY_PERCENT_UNKNOWN);
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
    TEST_CHECK(state.notification.title[SIM_NOTIFICATION_TITLE_CAPACITY - 1U] ==
               '\0');
    TEST_CHECK(state.notification
                   .subtitle[SIM_NOTIFICATION_SUBTITLE_CAPACITY - 1U] == '\0');
    TEST_CHECK(state.notification
                   .message[SIM_NOTIFICATION_MESSAGE_CAPACITY - 1U] == '\0');
    TEST_CHECK(strlen(state.notification.title) == 94U);
    TEST_CHECK(strlen(state.notification.message) == 254U);
}

static void test_multibyte_utf8_truncation_boundaries(void)
{
    char valid_three_byte[128] = "AB";
    char partial_three_byte[128] = "A";
    char valid_four_byte[128] = "A- ";
    char partial_four_byte[128] = "A";
    size_t valid_three_length = 2U;
    size_t partial_three_length = 1U;
    size_t valid_four_length = 3U;
    size_t partial_four_length = 1U;

    for (size_t index = 0U; index < 31U; ++index) {
        memcpy(&valid_three_byte[valid_three_length], "\xE2\x80\x94", 3U);
        valid_three_length += 3U;
        memcpy(&partial_three_byte[partial_three_length], "\xE2\x80\x94", 3U);
        partial_three_length += 3U;
    }
    memcpy(&partial_three_byte[partial_three_length], "\xE2\x80\x94", 3U);
    partial_three_length += 3U;
    valid_three_byte[valid_three_length] = '\0';
    partial_three_byte[partial_three_length] = '\0';

    for (size_t index = 0U; index < 23U; ++index) {
        memcpy(&valid_four_byte[valid_four_length], "\xF0\x9F\x91\x8B", 4U);
        valid_four_length += 4U;
        memcpy(&partial_four_byte[partial_four_length], "\xF0\x9F\x91\x8B", 4U);
        partial_four_length += 4U;
    }
    memcpy(&partial_four_byte[partial_four_length], "\xF0\x9F\x91\x8B", 4U);
    partial_four_length += 4U;
    valid_four_byte[valid_four_length] = '\0';
    partial_four_byte[partial_four_length] = '\0';

    TEST_CHECK(simulator_reset(0) == SIM_OK);
    connect_at(1);
    TEST_CHECK(simulator_send_notification(valid_three_byte, "", "", 2) ==
               SIM_OK);
    simulator_snapshot_t state = snapshot();
    TEST_CHECK(strlen(state.notification.title) == 95U);
    TEST_CHECK(memcmp(&state.notification.title[92], "\xE2\x80\x94", 3U) ==
               0);

    TEST_CHECK(simulator_send_notification(partial_three_byte, "", "", 3) ==
               SIM_OK);
    state = snapshot();
    TEST_CHECK(strlen(state.notification.title) == 94U);
    TEST_CHECK(memcmp(&state.notification.title[91], "\xE2\x80\x94", 3U) ==
               0);

    TEST_CHECK(simulator_send_notification(valid_four_byte, "", "", 4) ==
               SIM_OK);
    TEST_CHECK(strcmp(snapshot().notification.title, "A-") == 0);
    TEST_CHECK(simulator_send_notification(partial_four_byte, "", "", 5) ==
               SIM_OK);
    TEST_CHECK(strcmp(snapshot().notification.title, "A") == 0);
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

static void test_display_plan_and_bitmap_font_bridge(void)
{
    simulator_display_plan_t plan;
    simulator_font_glyph_t glyph;

    TEST_CHECK(simulator_reset(0) == SIM_OK);
    TEST_CHECK(simulator_display_plan(NULL) == SIM_ERR_NULL_ARGUMENT);
    TEST_CHECK(simulator_font_glyph((uint32_t)'A', NULL) ==
               SIM_ERR_NULL_ARGUMENT);
    TEST_CHECK(simulator_font_glyph(UINT32_C(0x1F44B), &glyph) ==
               SIM_ERR_INVALID_ARGUMENT);

    connect_at(1);
    TEST_CHECK(simulator_set_battery(10U, 2) == SIM_OK);
    TEST_CHECK(simulator_send_notification("Алиса", "Чат", "Привет", 3) ==
               SIM_OK);
    TEST_CHECK(simulator_display_plan(&plan) == SIM_OK);
    TEST_CHECK(plan.lines[0].style == SIM_DISPLAY_LINE_WARNING);
    TEST_CHECK(plan.lines[0].length > 0U);
    TEST_CHECK(plan.lines[1].style == SIM_DISPLAY_LINE_TITLE);
    TEST_CHECK(plan.lines[1].glyphs[0] == UINT32_C(0x0410));

    TEST_CHECK(simulator_font_glyph(UINT32_C(0x0410), &glyph) == SIM_OK);
    bool has_pixel = false;
    for (size_t row = 0U; row < SIM_DISPLAY_FONT_HEIGHT; ++row) {
        has_pixel = has_pixel || glyph.rows[row] != 0U;
    }
    TEST_CHECK(has_pixel);
}

int main(void)
{
    test_operations_require_reset();
    test_reset_version_and_defaults();
    test_connection_battery_and_validation();
    test_notification_normalization_and_beep();
    test_overlong_utf8_is_safely_truncated();
    test_multibyte_utf8_truncation_boundaries();
    test_disconnected_and_hidden_previews();
    test_eviction_and_button_ordering();
    test_timeout_and_long_hold();
    test_display_plan_and_bitmap_font_bridge();
    return 0;
}
