#include "display_layout.h"

#include <string.h>

#define DISPLAY_LAYOUT_MAX_INPUT_CODEPOINTS 256U

static const char FALLBACK_MESSAGE[] = "Новое сообщение";
static const char FALLBACK_TELEGRAM[] = "Telegram";
static const char FALLBACK_EMPTY[] = "Новое сообщение в Telegram";

static bool continuation(uint8_t byte)
{
    return (byte & 0xC0U) == 0x80U;
}

bool display_utf8_next(const char **cursor, uint32_t *codepoint)
{
    if (cursor == NULL || *cursor == NULL || codepoint == NULL) {
        return false;
    }

    const uint8_t *input = (const uint8_t *)*cursor;
    while (*input != 0U) {
        const uint8_t first = *input;
        size_t length = 0U;
        uint32_t value = 0U;
        uint32_t minimum = 0U;

        if (first < 0x80U) {
            length = 1U;
            value = first;
        } else if (first >= 0xC2U && first <= 0xDFU) {
            length = 2U;
            value = first & 0x1FU;
            minimum = 0x80U;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            length = 3U;
            value = first & 0x0FU;
            minimum = 0x800U;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            length = 4U;
            value = first & 0x07U;
            minimum = 0x10000U;
        } else {
            ++input;
            continue;
        }

        bool valid = true;
        for (size_t index = 1U; index < length; ++index) {
            if (input[index] == 0U || !continuation(input[index])) {
                valid = false;
                break;
            }
            value = (value << 6U) | (uint32_t)(input[index] & 0x3FU);
        }
        if (!valid || value < minimum || value > 0x10FFFFU ||
            (value >= 0xD800U && value <= 0xDFFFU)) {
            ++input;
            continue;
        }

        input += length;
        *cursor = (const char *)input;
        *codepoint = value;
        return true;
    }

    *cursor = (const char *)input;
    return false;
}

void display_content_select(const notification_t *notification, display_content_t *content)
{
    if (content == NULL) {
        return;
    }
    if (notification == NULL) {
        content->title = FALLBACK_EMPTY;
        content->subtitle = "";
        content->message = "";
        return;
    }

    const bool has_title = notification->title[0] != '\0';
    const bool has_subtitle = notification->subtitle[0] != '\0';
    const bool has_message = notification->message[0] != '\0';

    if (!has_title && !has_subtitle && !has_message) {
        content->title = FALLBACK_EMPTY;
        content->subtitle = "";
        content->message = "";
    } else {
        content->title = has_title ? notification->title
                                   : (has_subtitle ? "" : FALLBACK_TELEGRAM);
        content->subtitle = has_subtitle ? notification->subtitle : "";
        content->message = has_message ? notification->message : FALLBACK_MESSAGE;
    }
}

static bool is_space(uint32_t codepoint)
{
    return codepoint == (uint32_t)' ' || codepoint == (uint32_t)'\n' ||
           codepoint == (uint32_t)'\r' || codepoint == (uint32_t)'\t';
}

static void add_ellipsis(display_text_layout_t *layout, size_t columns)
{
    if (layout->line_count == 0U || columns == 0U) {
        return;
    }

    const size_t line = layout->line_count - 1U;
    if (columns < 3U) {
        for (size_t index = 0U; index < columns; ++index) {
            layout->lines[line][index] = (uint32_t)'.';
        }
        layout->line_lengths[line] = columns;
        return;
    }

    const size_t prefix = columns - 3U;
    if (layout->line_lengths[line] > prefix) {
        layout->line_lengths[line] = prefix;
    }
    while (layout->line_lengths[line] < prefix) {
        layout->lines[line][layout->line_lengths[line]++] = (uint32_t)' ';
    }
    for (size_t index = 0U; index < 3U; ++index) {
        layout->lines[line][layout->line_lengths[line]++] = (uint32_t)'.';
    }
}

static bool next_line(display_text_layout_t *layout, size_t max_lines)
{
    if (layout->line_count >= max_lines) {
        return false;
    }
    ++layout->line_count;
    return true;
}

static bool append_word(display_text_layout_t *layout, const uint32_t *word,
                        size_t word_length, size_t columns, size_t max_lines)
{
    size_t offset = 0U;
    while (offset < word_length) {
        const size_t line = layout->line_count - 1U;
        size_t used = layout->line_lengths[line];
        if (used > 0U) {
            const size_t remaining = word_length - offset;
            if (remaining + 1U <= columns - used) {
                layout->lines[line][used++] = (uint32_t)' ';
                layout->line_lengths[line] = used;
            } else if (!next_line(layout, max_lines)) {
                return false;
            } else {
                continue;
            }
        }

        const size_t current = layout->line_count - 1U;
        used = layout->line_lengths[current];
        const size_t available = columns - used;
        const size_t remaining = word_length - offset;
        const size_t count = remaining < available ? remaining : available;
        memcpy(&layout->lines[current][used], &word[offset], count * sizeof(word[0]));
        layout->line_lengths[current] += count;
        offset += count;
        if (offset < word_length && !next_line(layout, max_lines)) {
            return false;
        }
    }
    return true;
}

void display_layout_text(const char *text, size_t columns, size_t max_lines,
                         display_text_layout_t *layout)
{
    if (layout == NULL) {
        return;
    }
    memset(layout, 0, sizeof(*layout));
    if (text == NULL || text[0] == '\0' || columns == 0U || max_lines == 0U) {
        return;
    }
    if (columns > DISPLAY_LAYOUT_MAX_COLUMNS) {
        columns = DISPLAY_LAYOUT_MAX_COLUMNS;
    }
    if (max_lines > DISPLAY_LAYOUT_MAX_LINES) {
        max_lines = DISPLAY_LAYOUT_MAX_LINES;
    }

    uint32_t filtered[DISPLAY_LAYOUT_MAX_INPUT_CODEPOINTS];
    size_t filtered_length = 0U;
    bool input_overflow = false;
    bool pending_space = false;
    const char *cursor = text;
    uint32_t codepoint;
    while (display_utf8_next(&cursor, &codepoint)) {
        if (is_space(codepoint)) {
            pending_space = filtered_length > 0U;
            continue;
        }
        const uint16_t *rows;
        if (!display_font_get_glyph(codepoint, &rows)) {
            continue;
        }
        if (pending_space && filtered_length < DISPLAY_LAYOUT_MAX_INPUT_CODEPOINTS) {
            filtered[filtered_length++] = (uint32_t)' ';
        }
        pending_space = false;
        if (filtered_length >= DISPLAY_LAYOUT_MAX_INPUT_CODEPOINTS) {
            input_overflow = true;
            break;
        }
        filtered[filtered_length++] = codepoint;
    }
    if (filtered_length == 0U) {
        return;
    }

    layout->line_count = 1U;
    size_t index = 0U;
    while (index < filtered_length) {
        while (index < filtered_length && filtered[index] == (uint32_t)' ') {
            ++index;
        }
        const size_t start = index;
        while (index < filtered_length && filtered[index] != (uint32_t)' ') {
            ++index;
        }
        if (start == index) {
            break;
        }
        if (!append_word(layout, &filtered[start], index - start, columns, max_lines)) {
            layout->truncated = true;
            break;
        }
    }

    if (input_overflow || index < filtered_length) {
        layout->truncated = true;
    }
    if (layout->truncated) {
        add_ellipsis(layout, columns);
    }
}
