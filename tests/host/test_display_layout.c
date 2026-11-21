#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "display_layout.h"
#include "test_check.h"

static void check_line(const display_text_layout_t *layout, size_t line,
                       const char *ascii)
{
    const size_t length = strlen(ascii);
    TEST_CHECK(line < layout->line_count);
    TEST_CHECK(layout->line_lengths[line] == length);
    for (size_t index = 0U; index < length; ++index) {
        TEST_CHECK(layout->lines[line][index] == (uint32_t)(unsigned char)ascii[index]);
    }
}

static void check_render_line(const display_render_plan_t *plan, size_t line,
                              const char *expected)
{
    const char *cursor = expected;
    uint32_t codepoint;
    size_t index = 0U;
    while (display_utf8_next(&cursor, &codepoint)) {
        TEST_CHECK(index < plan->lines[line].length);
        TEST_CHECK(plan->lines[line].glyphs[index] == codepoint);
        ++index;
    }
    TEST_CHECK(plan->lines[line].length == index);
}

static void test_utf8_decoder(void)
{
    const char valid[] = "A\xD0\xAF\xD1\x8F";
    const char *cursor = valid;
    uint32_t codepoint = 0U;
    TEST_CHECK(display_utf8_next(&cursor, &codepoint));
    TEST_CHECK(codepoint == (uint32_t)'A');
    TEST_CHECK(display_utf8_next(&cursor, &codepoint));
    TEST_CHECK(codepoint == 0x042FU);
    TEST_CHECK(display_utf8_next(&cursor, &codepoint));
    TEST_CHECK(codepoint == 0x044FU);
    TEST_CHECK(!display_utf8_next(&cursor, &codepoint));

    const char malformed[] = "\xC0\xAF\xED\xA0\x80\xF4\x90\x80\x80Z";
    cursor = malformed;
    TEST_CHECK(display_utf8_next(&cursor, &codepoint));
    TEST_CHECK(codepoint == (uint32_t)'Z');
    TEST_CHECK(!display_utf8_next(&cursor, &codepoint));

    const char truncated[] = "\xE2\x82";
    cursor = truncated;
    TEST_CHECK(!display_utf8_next(&cursor, &codepoint));
}

static void test_font_coverage(void)
{
    const uint16_t *rows = NULL;
    for (uint32_t codepoint = 0x20U; codepoint <= 0x7EU; ++codepoint) {
        TEST_CHECK(display_font_get_glyph(codepoint, &rows));
        TEST_CHECK(rows != NULL);
        if (codepoint != 0x20U) {
            uint16_t pixels = 0U;
            for (size_t row = 0U; row < DISPLAY_FONT_HEIGHT; ++row) {
                pixels |= rows[row];
            }
            TEST_CHECK(pixels != 0U);
        }
    }
    for (uint32_t codepoint = 0x0400U; codepoint <= 0x04FFU; ++codepoint) {
        TEST_CHECK(display_font_get_glyph(codepoint, &rows));
        TEST_CHECK(rows != NULL);
        uint16_t pixels = 0U;
        for (size_t row = 0U; row < DISPLAY_FONT_HEIGHT; ++row) {
            pixels |= rows[row];
        }
        TEST_CHECK(pixels != 0U);
    }
    TEST_CHECK(!display_font_get_glyph(0x1F600U, &rows));
}

static void test_content_fallbacks(void)
{
    notification_t notification = {0};
    display_content_t content;

    strcpy(notification.title, "Alice");
    strcpy(notification.subtitle, "Team");
    strcpy(notification.message, "Hello");
    display_content_select(&notification, &content);
    TEST_CHECK(strcmp(content.title, "Alice") == 0);
    TEST_CHECK(strcmp(content.subtitle, "Team") == 0);
    TEST_CHECK(strcmp(content.message, "Hello") == 0);

    memset(&notification, 0, sizeof(notification));
    strcpy(notification.subtitle, "Team");
    display_content_select(&notification, &content);
    TEST_CHECK(content.title[0] == '\0');
    TEST_CHECK(strcmp(content.subtitle, "Team") == 0);
    TEST_CHECK(strcmp(content.message, "Новое сообщение") == 0);

    memset(&notification, 0, sizeof(notification));
    strcpy(notification.message, "Hello");
    display_content_select(&notification, &content);
    TEST_CHECK(strcmp(content.title, "Telegram") == 0);
    TEST_CHECK(content.subtitle[0] == '\0');
    TEST_CHECK(strcmp(content.message, "Hello") == 0);

    memset(&notification, 0, sizeof(notification));
    display_content_select(&notification, &content);
    TEST_CHECK(content.title[0] == '\0');
    TEST_CHECK(content.subtitle[0] == '\0');
    TEST_CHECK(strcmp(content.message, "Новое сообщение в Telegram") == 0);
}

static void test_low_battery_render_plan(void)
{
    display_render_plan_t plan;
    display_render_plan_make(DISPLAY_CONNECTION_CONNECTED, 15U, 3U, 5U, NULL,
                             &plan);
    TEST_CHECK(plan.lines[0].style == DISPLAY_LINE_WARNING);
    check_render_line(&plan, 0U, "Подключено !15% 3/5");

    display_render_plan_make(DISPLAY_CONNECTION_CONNECTED, 16U, 3U, 5U, NULL,
                             &plan);
    TEST_CHECK(plan.lines[0].style == DISPLAY_LINE_STATUS);
    check_render_line(&plan, 0U, "Подключено 16% 3/5");

    display_render_plan_make(DISPLAY_CONNECTION_CONNECTED, UINT8_MAX, 3U, 5U,
                             NULL, &plan);
    TEST_CHECK(plan.lines[0].style == DISPLAY_LINE_STATUS);
    check_render_line(&plan, 0U, "Подключено --% 3/5");
}

static void test_empty_notification_render_plan(void)
{
    const notification_t notification = {0};
    display_render_plan_t plan;
    display_render_plan_make(DISPLAY_CONNECTION_CONNECTED, 80U, 1U, 1U,
                             &notification, &plan);

    TEST_CHECK(plan.lines[1].style == DISPLAY_LINE_BODY);
    TEST_CHECK(plan.lines[2].style == DISPLAY_LINE_BODY);
    check_render_line(&plan, 1U, "Новое сообщение в");
    check_render_line(&plan, 2U, "Telegram");
    TEST_CHECK(plan.lines[3].length == 0U);
}

static void test_normal_notification_render_plan(void)
{
    notification_t notification = {0};
    strcpy(notification.title, "Title");
    strcpy(notification.subtitle, "Subtitle");
    strcpy(notification.message, "Message body");
    display_render_plan_t plan;
    display_render_plan_make(DISPLAY_CONNECTION_CONNECTED, 80U, 2U, 5U,
                             &notification, &plan);

    TEST_CHECK(plan.lines[1].style == DISPLAY_LINE_TITLE);
    TEST_CHECK(plan.lines[2].style == DISPLAY_LINE_SUBTITLE);
    TEST_CHECK(plan.lines[3].style == DISPLAY_LINE_BODY);
    check_render_line(&plan, 1U, "Title");
    check_render_line(&plan, 2U, "Subtitle");
    check_render_line(&plan, 3U, "Message body");
}

static void test_wrapping_and_ellipsis(void)
{
    display_text_layout_t layout;
    display_layout_text("one two three", 7U, 3U, &layout);
    TEST_CHECK(!layout.truncated);
    TEST_CHECK(layout.line_count == 2U);
    check_line(&layout, 0U, "one two");
    check_line(&layout, 1U, "three");

    display_layout_text("one two three four", 7U, 2U, &layout);
    TEST_CHECK(layout.truncated);
    TEST_CHECK(layout.line_count == 2U);
    check_line(&layout, 0U, "one two");
    check_line(&layout, 1U, "thre...");

    display_layout_text("abcdefghij", 4U, 2U, &layout);
    TEST_CHECK(layout.truncated);
    check_line(&layout, 0U, "abcd");
    check_line(&layout, 1U, "e...");

    display_layout_text("A\xF0\x9F\x98\x80 B", 4U, 1U, &layout);
    TEST_CHECK(!layout.truncated);
    check_line(&layout, 0U, "A B");
}

int main(void)
{
    test_utf8_decoder();
    test_font_coverage();
    test_content_fallbacks();
    test_low_battery_render_plan();
    test_empty_notification_render_plan();
    test_normal_notification_render_plan();
    test_wrapping_and_ellipsis();
    return 0;
}
