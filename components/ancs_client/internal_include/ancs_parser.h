#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ancs_client.h"

#define ANCS_PARSER_TIMEOUT_MS 3000U

typedef enum {
    ANCS_PARSER_RESPONSE_HEADER,
    ANCS_PARSER_ATTRIBUTE_HEADER,
    ANCS_PARSER_ATTRIBUTE_VALUE
} ancs_parser_stage_t;

typedef struct {
    notification_t notification;
    ancs_notification_cb_t callback;
    void *callback_ctx;
    uint32_t expected_uid;
    uint32_t deadline_ms;
    uint32_t generation;
    uint16_t attribute_length;
    uint16_t attribute_offset;
    uint8_t response_header[5];
    uint8_t attribute_header[3];
    uint8_t response_header_length;
    uint8_t attribute_header_length;
    uint8_t received_mask;
    uint8_t attribute_id;
    ancs_parser_stage_t stage;
    bool foreign_response;
    bool resync_expected_header;
    bool pending;
} ancs_parser_t;

void ancs_parser_init(ancs_parser_t *parser, ancs_notification_cb_t callback,
                      void *callback_ctx);
void ancs_parser_expect(ancs_parser_t *parser, uint32_t uid, uint32_t now_ms);
void ancs_parser_feed(ancs_parser_t *parser, const uint8_t *data, size_t length,
                      uint32_t now_ms);
void ancs_parser_expire(ancs_parser_t *parser, uint32_t now_ms);
void ancs_parser_cancel(ancs_parser_t *parser, uint32_t uid);
bool ancs_parser_is_pending(const ancs_parser_t *parser);
