#include <stdint.h>
#include <string.h>

#include "ancs_parser.h"
#include "ancs_request_queue.h"
#include "test_check.h"

typedef struct {
    ancs_parser_t *parser;
    unsigned int calls;
    uint32_t uid;
    uint32_t next_uid;
} capture_t;

static void capture_notification(const notification_t *notification, void *ctx)
{
    capture_t *capture = ctx;

    capture->calls++;
    capture->uid = notification->uid;
    if (capture->next_uid != 0U) {
        ancs_parser_expect(capture->parser, capture->next_uid, 10U);
    }
}

static size_t append_attribute(uint8_t *buffer, size_t offset, uint8_t id,
                               const char *value)
{
    const size_t length = strlen(value);

    buffer[offset++] = id;
    buffer[offset++] = (uint8_t)length;
    buffer[offset++] = 0U;
    memcpy(&buffer[offset], value, length);
    return offset + length;
}

static size_t make_response(uint8_t *buffer, uint32_t uid)
{
    size_t length = 0U;

    buffer[length++] = 0U;
    buffer[length++] = (uint8_t)uid;
    buffer[length++] = (uint8_t)(uid >> 8U);
    buffer[length++] = (uint8_t)(uid >> 16U);
    buffer[length++] = (uint8_t)(uid >> 24U);
    length = append_attribute(buffer, length, 0U, "app");
    length = append_attribute(buffer, length, 1U, "title");
    length = append_attribute(buffer, length, 2U, "subtitle");
    return append_attribute(buffer, length, 3U, "message");
}

static void test_late_response_preserves_active_request(void)
{
    ancs_parser_t parser;
    capture_t capture = {.parser = &parser};
    uint8_t late[96];
    uint8_t expected[96];
    const size_t late_length = make_response(late, 1U);
    const size_t expected_length = make_response(expected, 2U);
    const uint8_t trailing[] = {0xdeU, 0xadU};

    ancs_parser_init(&parser, capture_notification, &capture);
    ancs_parser_expect(&parser, 1U, 0U);
    ancs_parser_expire(&parser, ANCS_PARSER_TIMEOUT_MS);
    ancs_parser_expect(&parser, 2U, ANCS_PARSER_TIMEOUT_MS + 1U);
    ancs_parser_feed(&parser, late, 6U, ANCS_PARSER_TIMEOUT_MS + 2U);
    ancs_parser_feed(&parser, &late[6], late_length - 6U,
                     ANCS_PARSER_TIMEOUT_MS + 3U);
    ancs_parser_feed(&parser, trailing, sizeof(trailing),
                     ANCS_PARSER_TIMEOUT_MS + 3U);
    TEST_CHECK(ancs_parser_is_pending(&parser));
    TEST_CHECK(capture.calls == 0U);
    ancs_parser_feed(&parser, expected, expected_length,
                     ANCS_PARSER_TIMEOUT_MS + 4U);
    TEST_CHECK(capture.calls == 1U);
    TEST_CHECK(capture.uid == 2U);
}

static void test_stale_attribute_tail_resynchronizes_to_active_uid(void)
{
    ancs_parser_t parser;
    capture_t capture = {.parser = &parser};
    uint8_t expected[96];
    const size_t expected_length = make_response(expected, 22U);
    const uint8_t stale_tail[] = {
        'l', 'd', ' ', 'm', 'e', 's', 's', 'a', 'g', 'e',
        3U, 4U, 0U, 't', 'a', 'i', 'l',
    };

    ancs_parser_init(&parser, capture_notification, &capture);
    ancs_parser_expect(&parser, 22U, 0U);
    ancs_parser_feed(&parser, stale_tail, 4U, 1U);
    ancs_parser_feed(&parser, &stale_tail[4], sizeof(stale_tail) - 4U, 2U);
    TEST_CHECK(ancs_parser_is_pending(&parser));
    TEST_CHECK(capture.calls == 0U);
    ancs_parser_feed(&parser, expected, 3U, 3U);
    ancs_parser_feed(&parser, &expected[3], expected_length - 3U, 4U);
    TEST_CHECK(capture.calls == 1U);
    TEST_CHECK(capture.uid == 22U);
}

static void test_overlapping_zero_prefix_resynchronizes_at_every_split(void)
{
    static const uint8_t stale_zeros[] = {0U, 0U, 0U, 0U, 0U, 0U, 0U};
    const size_t prefix_lengths[] = {1U, 2U, sizeof(stale_zeros)};
    const uint32_t uid = 0x00000100U;
    uint8_t expected[96];
    const size_t expected_length = make_response(expected, uid);

    for (size_t prefix_index = 0U;
         prefix_index < sizeof(prefix_lengths) / sizeof(prefix_lengths[0]);
         ++prefix_index) {
        for (size_t split = 0U; split <= 5U; ++split) {
            ancs_parser_t parser;
            capture_t capture = {.parser = &parser};

            ancs_parser_init(&parser, capture_notification, &capture);
            ancs_parser_expect(&parser, uid, 0U);
            for (size_t index = 0U; index < prefix_lengths[prefix_index];
                 ++index) {
                ancs_parser_feed(&parser, &stale_zeros[index], 1U, 1U);
            }
            ancs_parser_feed(&parser, expected, split, 2U);
            ancs_parser_feed(&parser, &expected[split], expected_length - split,
                             3U);

            TEST_CHECK(capture.calls == 1U);
            TEST_CHECK(capture.uid == uid);
        }
    }
}

static void test_completion_ignores_trailing_bytes_after_reset(void)
{
    ancs_parser_t parser;
    capture_t capture = {.parser = &parser, .next_uid = 4U};
    uint8_t first[96];
    uint8_t second[96];
    size_t first_length = make_response(first, 3U);
    const size_t second_length = make_response(second, 4U);

    first[first_length++] = 0xffU;
    first[first_length++] = 0xeeU;
    ancs_parser_init(&parser, capture_notification, &capture);
    ancs_parser_expect(&parser, 3U, 0U);
    ancs_parser_feed(&parser, first, first_length, 1U);
    TEST_CHECK(capture.calls == 1U);
    TEST_CHECK(ancs_parser_is_pending(&parser));
    capture.next_uid = 0U;
    ancs_parser_feed(&parser, second, second_length, 11U);
    TEST_CHECK(capture.calls == 2U);
    TEST_CHECK(capture.uid == 4U);
}

static void test_queue_preserves_repeated_events_and_removes_all_uid(void)
{
    ancs_request_queue_t queue;
    uint32_t uid = 0U;

    ancs_request_queue_init(&queue);
    TEST_CHECK(ancs_request_queue_push(&queue, 7U));
    TEST_CHECK(ancs_request_queue_push(&queue, 8U));
    TEST_CHECK(ancs_request_queue_push(&queue, 7U));
    TEST_CHECK(ancs_request_queue_push(&queue, 9U));
    TEST_CHECK(ancs_request_queue_remove(&queue, 7U) == 2U);
    TEST_CHECK(ancs_request_queue_pop(&queue, &uid) && uid == 8U);
    TEST_CHECK(ancs_request_queue_pop(&queue, &uid) && uid == 9U);
    TEST_CHECK(!ancs_request_queue_pop(&queue, &uid));
}

int main(void)
{
    test_late_response_preserves_active_request();
    test_stale_attribute_tail_resynchronizes_to_active_uid();
    test_overlapping_zero_prefix_resynchronizes_at_every_split();
    test_completion_ignores_trailing_bytes_after_reset();
    test_queue_preserves_repeated_events_and_removes_all_uid();
    return 0;
}
