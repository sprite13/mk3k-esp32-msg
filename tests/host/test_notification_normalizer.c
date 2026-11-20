#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "notification_normalizer.h"

static void assert_repeated_prefix(const char *value, char expected,
                                   size_t count)
{
    for (size_t index = 0; index < count; ++index) {
        assert(value[index] == expected);
    }
    assert(value[count] == '\0');
}

static void test_exact_telegram_bundle_id(void)
{
    notification_t item = {0};
    const size_t bundle_length = strlen(TELEGRAM_IOS_BUNDLE_ID);

    strcpy(item.app_id, TELEGRAM_IOS_BUNDLE_ID);
    assert(notification_is_telegram(&item));

    strcpy(item.app_id, "ph.telegra.Telegraph.beta");
    assert(!notification_is_telegram(&item));

    strcpy(item.app_id, "ph.telegra.Telegrap");
    assert(!notification_is_telegram(&item));

    memset(item.app_id, 'x', sizeof(item.app_id));
    assert(!notification_is_telegram(&item));

    memcpy(item.app_id, TELEGRAM_IOS_BUNDLE_ID, bundle_length);
    assert(!notification_is_telegram(&item));
}

static void test_preserves_text_and_removes_emoji(void)
{
    notification_t item = {0};

    strcpy(item.title, "Стас 👋");
    strcpy(item.subtitle, "Ѐ ӿ «Ответ» — да!");
    strcpy(item.message, "Привет 🚀 test");

    notification_normalize(&item);

    assert(strcmp(item.title, "Стас") == 0);
    assert(strcmp(item.subtitle, "Ѐ ӿ «Ответ» — да!") == 0);
    assert(strcmp(item.message, "Привет test") == 0);
}

static void test_removes_unsupported_valid_unicode(void)
{
    notification_t item = {0};

    strcpy(item.message, "AéB漢Cα — «да»…");

    notification_normalize(&item);

    assert(strcmp(item.message, "ABC — «да»…") == 0);
}

static void test_skips_invalid_utf8_one_byte_at_a_time(void)
{
    notification_t item = {0};
    const char malformed[] = {
        'b', 'a', 'd', (char)0xF0, '(', (char)0x8C, '(',
        't', 'e', 'x', 't', '\0',
    };
    const char truncated[] = {'x', (char)0xE2, (char)0x82, '\0'};
    const char invalid_ranges[] = {
        'a',
        (char)0xC0, (char)0xAF,
        'b',
        (char)0xED, (char)0xA0, (char)0x80,
        'c',
        (char)0xF4, (char)0x90, (char)0x80, (char)0x80,
        'd',
        '\0',
    };

    memcpy(item.message, malformed, sizeof(malformed));
    memcpy(item.title, truncated, sizeof(truncated));
    memcpy(item.subtitle, invalid_ranges, sizeof(invalid_ranges));

    notification_normalize(&item);

    assert(strcmp(item.message, "bad((text") == 0);
    assert(strcmp(item.title, "x") == 0);
    assert(strcmp(item.subtitle, "abcd") == 0);
}

static void test_removes_emoji_joiners_and_variation_selectors(void)
{
    notification_t item = {0};
    const char controls[] = {
        'A',
        (char)0xEF, (char)0xB8, (char)0x8F,
        (char)0xE2, (char)0x80, (char)0x8D,
        (char)0xF3, (char)0xA0, (char)0x84, (char)0x80,
        'B',
        '\0',
    };

    strcpy(item.title, "👋🚀");
    memcpy(item.message, controls, sizeof(controls));

    notification_normalize(&item);

    assert(strcmp(item.title, "") == 0);
    assert(strcmp(item.message, "AB") == 0);
}

static void test_collapses_and_trims_ascii_whitespace(void)
{
    notification_t item = {0};

    strcpy(item.message, " \t\n  Привет\r\n\v test \f ");

    notification_normalize(&item);

    assert(strcmp(item.message, "Привет test") == 0);
}

static void test_respects_output_bounds_and_terminates_fields(void)
{
    notification_t item;

    memset(&item, 0, sizeof(item));
    memset(item.app_id, 'd', sizeof(item.app_id));
    memset(item.title, 'a', sizeof(item.title));
    item.title[sizeof(item.title) - 2U] = (char)0xD0;
    item.title[sizeof(item.title) - 1U] = (char)0x90;
    memset(item.subtitle, 'b', sizeof(item.subtitle));
    memset(item.message, 'c', sizeof(item.message));

    notification_normalize(&item);

    assert_repeated_prefix(item.app_id, 'd', sizeof(item.app_id) - 1U);
    assert_repeated_prefix(item.title, 'a', sizeof(item.title) - 2U);
    assert_repeated_prefix(item.subtitle, 'b', sizeof(item.subtitle) - 1U);
    assert_repeated_prefix(item.message, 'c', sizeof(item.message) - 1U);
}

int main(void)
{
    test_exact_telegram_bundle_id();
    test_preserves_text_and_removes_emoji();
    test_skips_invalid_utf8_one_byte_at_a_time();
    test_removes_emoji_joiners_and_variation_selectors();
    test_collapses_and_trims_ascii_whitespace();
    test_respects_output_bounds_and_terminates_fields();
    test_removes_unsupported_valid_unicode();
    return 0;
}
