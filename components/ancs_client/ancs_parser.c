#include "ancs_parser.h"

#include <string.h>

#define ANCS_COMMAND_GET_NOTIFICATION_ATTRIBUTES 0U
#define ANCS_ATTRIBUTE_APP_IDENTIFIER 0U
#define ANCS_ATTRIBUTE_TITLE 1U
#define ANCS_ATTRIBUTE_SUBTITLE 2U
#define ANCS_ATTRIBUTE_MESSAGE 3U
#define ANCS_REQUESTED_ATTRIBUTE_MASK 0x0fU

static uint32_t read_le32(const uint8_t *value)
{
    return (uint32_t)value[0] |
           ((uint32_t)value[1] << 8U) |
           ((uint32_t)value[2] << 16U) |
           ((uint32_t)value[3] << 24U);
}

static void parser_reset_response(ancs_parser_t *parser)
{
    memset(&parser->notification, 0, sizeof(parser->notification));
    parser->notification.uid = parser->expected_uid;
    parser->attribute_length = 0U;
    parser->attribute_offset = 0U;
    parser->response_header_length = 0U;
    parser->attribute_header_length = 0U;
    parser->received_mask = 0U;
    parser->attribute_id = 0U;
    parser->stage = ANCS_PARSER_RESPONSE_HEADER;
    parser->foreign_response = false;
    parser->resync_expected_header = false;
}

static void parser_drop(ancs_parser_t *parser)
{
    parser->pending = false;
    parser_reset_response(parser);
}

static char *attribute_destination(ancs_parser_t *parser, uint8_t id,
                                   size_t *capacity)
{
    switch (id) {
    case ANCS_ATTRIBUTE_APP_IDENTIFIER:
        *capacity = sizeof(parser->notification.app_id);
        return parser->notification.app_id;
    case ANCS_ATTRIBUTE_TITLE:
        *capacity = sizeof(parser->notification.title);
        return parser->notification.title;
    case ANCS_ATTRIBUTE_SUBTITLE:
        *capacity = sizeof(parser->notification.subtitle);
        return parser->notification.subtitle;
    case ANCS_ATTRIBUTE_MESSAGE:
        *capacity = sizeof(parser->notification.message);
        return parser->notification.message;
    default:
        *capacity = 0U;
        return NULL;
    }
}

static bool finish_attribute(ancs_parser_t *parser)
{
    size_t capacity = 0U;
    char *destination = attribute_destination(parser, parser->attribute_id,
                                              &capacity);

    (void)capacity;
    if (destination != NULL && !parser->foreign_response) {
        destination[parser->attribute_length] = '\0';
    }
    if (destination != NULL) {
        parser->received_mask |= (uint8_t)(1U << parser->attribute_id);
    }

    parser->attribute_header_length = 0U;
    parser->attribute_offset = 0U;
    parser->stage = ANCS_PARSER_ATTRIBUTE_HEADER;

    if (parser->received_mask == ANCS_REQUESTED_ATTRIBUTE_MASK) {
        if (parser->foreign_response) {
            parser_reset_response(parser);
            parser->resync_expected_header = true;
            return false;
        }
        parser->pending = false;
        if (parser->callback != NULL) {
            parser->callback(&parser->notification, parser->callback_ctx);
        }
        return true;
    }
    return false;
}

static bool consume_response_header(ancs_parser_t *parser, uint8_t byte)
{
    if (parser->resync_expected_header) {
        const uint8_t expected[] = {
            ANCS_COMMAND_GET_NOTIFICATION_ATTRIBUTES,
            (uint8_t)(parser->expected_uid & 0xffU),
            (uint8_t)((parser->expected_uid >> 8U) & 0xffU),
            (uint8_t)((parser->expected_uid >> 16U) & 0xffU),
            (uint8_t)((parser->expected_uid >> 24U) & 0xffU),
        };

        if (byte == expected[parser->response_header_length]) {
            parser->response_header_length++;
            if (parser->response_header_length == sizeof(expected)) {
                parser->response_header_length = 0U;
                parser->resync_expected_header = false;
                parser->stage = ANCS_PARSER_ATTRIBUTE_HEADER;
            }
        } else {
            parser->response_header_length =
                byte == ANCS_COMMAND_GET_NOTIFICATION_ATTRIBUTES ? 1U : 0U;
        }
        return false;
    }

    parser->response_header[parser->response_header_length++] = byte;
    if (parser->response_header_length != sizeof(parser->response_header)) {
        return false;
    }

    if (parser->response_header[0] != ANCS_COMMAND_GET_NOTIFICATION_ATTRIBUTES) {
        parser_drop(parser);
        return true;
    }

    parser->foreign_response =
        read_le32(&parser->response_header[1]) != parser->expected_uid;
    parser->stage = ANCS_PARSER_ATTRIBUTE_HEADER;
    return false;
}

static bool consume_attribute_header(ancs_parser_t *parser, uint8_t byte)
{
    size_t capacity = 0U;

    parser->attribute_header[parser->attribute_header_length++] = byte;
    if (parser->attribute_header_length != sizeof(parser->attribute_header)) {
        return false;
    }

    parser->attribute_id = parser->attribute_header[0];
    parser->attribute_length = (uint16_t)parser->attribute_header[1] |
                               ((uint16_t)parser->attribute_header[2] << 8U);
    (void)attribute_destination(parser, parser->attribute_id, &capacity);
    if (!parser->foreign_response && capacity > 0U &&
        parser->attribute_length >= capacity) {
        parser_drop(parser);
        return true;
    }

    parser->attribute_offset = 0U;
    parser->stage = ANCS_PARSER_ATTRIBUTE_VALUE;
    if (parser->attribute_length == 0U) {
        return finish_attribute(parser);
    }
    return false;
}

static bool consume_attribute_value(ancs_parser_t *parser, uint8_t byte)
{
    size_t capacity = 0U;
    char *destination = attribute_destination(parser, parser->attribute_id,
                                              &capacity);

    if (destination != NULL && !parser->foreign_response) {
        destination[parser->attribute_offset] = (char)byte;
    }
    parser->attribute_offset++;
    if (parser->attribute_offset == parser->attribute_length) {
        return finish_attribute(parser);
    }
    return false;
}

void ancs_parser_init(ancs_parser_t *parser, ancs_notification_cb_t callback,
                      void *callback_ctx)
{
    memset(parser, 0, sizeof(*parser));
    parser->callback = callback;
    parser->callback_ctx = callback_ctx;
}

void ancs_parser_expect(ancs_parser_t *parser, uint32_t uid, uint32_t now_ms)
{
    parser->generation++;
    parser->expected_uid = uid;
    parser->deadline_ms = now_ms + ANCS_PARSER_TIMEOUT_MS;
    parser_reset_response(parser);
    parser->resync_expected_header = true;
    parser->pending = true;
}

void ancs_parser_feed(ancs_parser_t *parser, const uint8_t *data, size_t length,
                      uint32_t now_ms)
{
    uint32_t generation;

    ancs_parser_expire(parser, now_ms);
    if (!parser->pending || data == NULL) {
        return;
    }

    generation = parser->generation;
    for (size_t index = 0U;
         index < length && parser->pending && parser->generation == generation;
         ++index) {
        bool stop = false;

        switch (parser->stage) {
        case ANCS_PARSER_RESPONSE_HEADER:
            stop = consume_response_header(parser, data[index]);
            break;
        case ANCS_PARSER_ATTRIBUTE_HEADER:
            stop = consume_attribute_header(parser, data[index]);
            break;
        case ANCS_PARSER_ATTRIBUTE_VALUE:
            stop = consume_attribute_value(parser, data[index]);
            break;
        }
        if (stop) {
            break;
        }
    }
}

void ancs_parser_expire(ancs_parser_t *parser, uint32_t now_ms)
{
    if (parser->pending && (int32_t)(now_ms - parser->deadline_ms) >= 0) {
        parser_drop(parser);
    }
}

void ancs_parser_cancel(ancs_parser_t *parser, uint32_t uid)
{
    if (parser->pending && parser->expected_uid == uid) {
        parser_drop(parser);
    }
}

bool ancs_parser_is_pending(const ancs_parser_t *parser)
{
    return parser->pending;
}
