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
    /* A known battery percentage at or below this value uses warning styling. */
    DISPLAY_LOW_BATTERY_PERCENT = 15,
};

typedef enum {
    DISPLAY_CONNECTION_PAIRING,
    DISPLAY_CONNECTION_CONNECTED,
    DISPLAY_CONNECTION_RECONNECTING,
    DISPLAY_CONNECTION_ERROR,
} display_connection_t;

typedef enum {
    DISPLAY_LINE_STATUS,
    DISPLAY_LINE_WARNING,
    DISPLAY_LINE_TITLE,
    DISPLAY_LINE_SUBTITLE,
    DISPLAY_LINE_BODY,
} display_line_style_t;

typedef struct {
    uint32_t glyphs[DISPLAY_LAYOUT_MAX_COLUMNS];
    size_t length;
    display_line_style_t style;
} display_render_line_t;

typedef struct {
    display_render_line_t lines[DISPLAY_LAYOUT_MAX_LINES];
} display_render_plan_t;

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
/* Battery values 0..100 are percentages; values above 100 mean unknown. */
void display_render_plan_make(display_connection_t connection, uint8_t battery_percent,
                              size_t position, size_t total,
                              const notification_t *notification,
                              display_render_plan_t *plan);
