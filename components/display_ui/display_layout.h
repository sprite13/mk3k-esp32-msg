#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "notification.h"

enum {
    DISPLAY_FONT_WIDTH = 12,
    DISPLAY_FONT_HEIGHT = 20,
    DISPLAY_LAYOUT_MAX_COLUMNS = 20,
    DISPLAY_LAYOUT_MAX_LINES = 6,
};

typedef struct {
    uint32_t lines[DISPLAY_LAYOUT_MAX_LINES][DISPLAY_LAYOUT_MAX_COLUMNS];
    size_t line_lengths[DISPLAY_LAYOUT_MAX_LINES];
    size_t line_count;
    bool truncated;
} display_text_layout_t;

typedef struct {
    const char *title;
    const char *subtitle;
    const char *message;
} display_content_t;

bool display_utf8_next(const char **cursor, uint32_t *codepoint);
bool display_font_get_glyph(uint32_t codepoint, const uint16_t **rows);
void display_content_select(const notification_t *notification, display_content_t *content);
void display_layout_text(const char *text, size_t columns, size_t max_lines,
                         display_text_layout_t *layout);
