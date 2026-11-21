#include <stdint.h>
#include <string.h>

#include "ancs_client.h"
#include "ancs_parser.h"
#include "unity.h"

typedef struct {
    unsigned int calls;
    notification_t notification;
} callback_capture_t;

static void capture_notification(const notification_t *notification, void *ctx)
{
    callback_capture_t *capture = ctx;

    capture->calls++;
    capture->notification = *notification;
}

static size_t append_attribute(uint8_t *buffer, size_t offset, uint8_t id,
                               const char *value)
{
    const size_t length = strlen(value);

    buffer[offset++] = id;
    buffer[offset++] = (uint8_t)(length & 0xffU);
    buffer[offset++] = (uint8_t)(length >> 8U);
    memcpy(&buffer[offset], value, length);
    return offset + length;
}

static size_t make_complete_response(uint8_t *buffer, uint32_t uid)
{
    size_t length = 0U;

    buffer[length++] = 0U;
    buffer[length++] = (uint8_t)(uid & 0xffU);
    buffer[length++] = (uint8_t)((uid >> 8U) & 0xffU);
    buffer[length++] = (uint8_t)((uid >> 16U) & 0xffU);
    buffer[length++] = (uint8_t)((uid >> 24U) & 0xffU);
    length = append_attribute(buffer, length, 0U, "ph.telegra.Telegraph");
    length = append_attribute(buffer, length, 1U, "Chat");
    length = append_attribute(buffer, length, 2U, "Alice");
    return append_attribute(buffer, length, 3U, "Hello");
}

void app_main(void)
{
    unity_run_menu();
}

TEST_CASE("fragmented data source emits one complete notification", "[ancs_parser]")
{
    ancs_parser_t parser;
    callback_capture_t capture = {0};
    uint8_t response[128];
    const uint32_t uid = 0x44332211U;
    const size_t length = make_complete_response(response, uid);

    ancs_parser_init(&parser, capture_notification, &capture);
    ancs_parser_expect(&parser, uid, 100U);
    ancs_parser_feed(&parser, response, 2U, 101U);
    ancs_parser_feed(&parser, &response[2], 3U, 102U);
    for (size_t i = 5U; i < length; ++i) {
        ancs_parser_feed(&parser, &response[i], 1U, 103U + i);
    }

    TEST_ASSERT_EQUAL_UINT(1U, capture.calls);
    TEST_ASSERT_EQUAL_HEX32(uid, capture.notification.uid);
    TEST_ASSERT_EQUAL_STRING("ph.telegra.Telegraph", capture.notification.app_id);
    TEST_ASSERT_EQUAL_STRING("Chat", capture.notification.title);
    TEST_ASSERT_EQUAL_STRING("Alice", capture.notification.subtitle);
    TEST_ASSERT_EQUAL_STRING("Hello", capture.notification.message);
}

TEST_CASE("unknown attribute is skipped without delaying completion", "[ancs_parser]")
{
    ancs_parser_t parser;
    callback_capture_t capture = {0};
    uint8_t response[160];
    const uint32_t uid = 9U;
    size_t length = make_complete_response(response, uid);
    const uint8_t unknown[] = {99U, 7U, 0U, 'i', 'g', 'n', 'o', 'r', 'e', 'd'};

    memmove(&response[5U + sizeof(unknown)], &response[5U], length - 5U);
    memcpy(&response[5U], unknown, sizeof(unknown));
    length += sizeof(unknown);
    ancs_parser_init(&parser, capture_notification, &capture);
    ancs_parser_expect(&parser, uid, 0U);
    ancs_parser_feed(&parser, response, length, 1U);

    TEST_ASSERT_EQUAL_UINT(1U, capture.calls);
}

TEST_CASE("oversize requested attribute drops response", "[ancs_parser]")
{
    ancs_parser_t parser;
    callback_capture_t capture = {0};
    uint8_t response[] = {0U, 7U, 0U, 0U, 0U,
                          1U, 0xffU, 0x00U};

    ancs_parser_init(&parser, capture_notification, &capture);
    ancs_parser_expect(&parser, 7U, 0U);
    ancs_parser_feed(&parser, response, sizeof(response), 1U);

    TEST_ASSERT_EQUAL_UINT(0U, capture.calls);
    TEST_ASSERT_FALSE(ancs_parser_is_pending(&parser));
}

TEST_CASE("malformed command and truncated response never emit", "[ancs_parser]")
{
    ancs_parser_t parser;
    callback_capture_t capture = {0};
    const uint8_t malformed[] = {3U, 8U, 0U, 0U, 0U};
    const uint8_t truncated[] = {0U, 8U, 0U, 0U, 0U, 0U, 5U, 0U, 'a'};

    ancs_parser_init(&parser, capture_notification, &capture);
    ancs_parser_expect(&parser, 8U, 0U);
    ancs_parser_feed(&parser, malformed, sizeof(malformed), 1U);
    TEST_ASSERT_FALSE(ancs_parser_is_pending(&parser));

    ancs_parser_expect(&parser, 8U, 10U);
    ancs_parser_feed(&parser, truncated, sizeof(truncated), 11U);
    ancs_parser_expire(&parser, 3011U);
    TEST_ASSERT_EQUAL_UINT(0U, capture.calls);
    TEST_ASSERT_FALSE(ancs_parser_is_pending(&parser));
}

TEST_CASE("three second timeout cancels partial response", "[ancs_parser]")
{
    ancs_parser_t parser;
    callback_capture_t capture = {0};
    const uint8_t fragment[] = {0U, 4U};

    ancs_parser_init(&parser, capture_notification, &capture);
    ancs_parser_expect(&parser, 4U, 100U);
    ancs_parser_feed(&parser, fragment, sizeof(fragment), 200U);
    ancs_parser_expire(&parser, 3099U);
    TEST_ASSERT_TRUE(ancs_parser_is_pending(&parser));
    ancs_parser_expire(&parser, 3100U);
    TEST_ASSERT_FALSE(ancs_parser_is_pending(&parser));
    TEST_ASSERT_EQUAL_UINT(0U, capture.calls);
}

TEST_CASE("uid mismatch is discarded and next expected uid succeeds", "[ancs_parser]")
{
    ancs_parser_t parser;
    callback_capture_t capture = {0};
    uint8_t wrong[128];
    uint8_t expected[128];
    const size_t wrong_length = make_complete_response(wrong, 12U);
    const size_t expected_length = make_complete_response(expected, 13U);

    ancs_parser_init(&parser, capture_notification, &capture);
    ancs_parser_expect(&parser, 13U, 0U);
    ancs_parser_feed(&parser, wrong, wrong_length, 1U);
    TEST_ASSERT_EQUAL_UINT(0U, capture.calls);
    TEST_ASSERT_FALSE(ancs_parser_is_pending(&parser));

    ancs_parser_expect(&parser, 13U, 2U);
    ancs_parser_feed(&parser, expected, 4U, 3U);
    ancs_parser_expect(&parser, 14U, 4U);
    ancs_parser_expect(&parser, 13U, 5U);
    ancs_parser_feed(&parser, expected, expected_length, 6U);
    TEST_ASSERT_EQUAL_UINT(1U, capture.calls);
    TEST_ASSERT_EQUAL_HEX32(13U, capture.notification.uid);
}

TEST_CASE("removed notification cancels matching uid only", "[ancs_parser]")
{
    ancs_parser_t parser;
    callback_capture_t capture = {0};
    uint8_t response[128];
    const size_t length = make_complete_response(response, 21U);

    ancs_parser_init(&parser, capture_notification, &capture);
    ancs_parser_expect(&parser, 21U, 0U);
    ancs_parser_cancel(&parser, 22U);
    TEST_ASSERT_TRUE(ancs_parser_is_pending(&parser));
    ancs_parser_cancel(&parser, 21U);
    TEST_ASSERT_FALSE(ancs_parser_is_pending(&parser));
    ancs_parser_feed(&parser, response, length, 1U);
    TEST_ASSERT_EQUAL_UINT(0U, capture.calls);
}
