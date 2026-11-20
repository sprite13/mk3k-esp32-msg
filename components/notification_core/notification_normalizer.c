#include "notification_normalizer.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t code_point;
    size_t length;
} utf8_character_t;

static bool is_continuation(uint8_t byte)
{
    return (byte & 0xC0U) == 0x80U;
}

static bool decode_utf8(const char *input, size_t remaining,
                        utf8_character_t *character)
{
    const uint8_t first = (uint8_t)input[0];

    if (first < 0x80U) {
        character->code_point = first;
        character->length = 1U;
        return true;
    }

    if (first >= 0xC2U && first <= 0xDFU && remaining >= 2U) {
        const uint8_t second = (uint8_t)input[1];
        if (second != 0U && is_continuation(second)) {
            character->code_point = ((uint32_t)(first & 0x1FU) << 6U) |
                                    (uint32_t)(second & 0x3FU);
            character->length = 2U;
            return true;
        }
    }

    if (first >= 0xE0U && first <= 0xEFU && remaining >= 3U) {
        const uint8_t second = (uint8_t)input[1];
        const uint8_t third = (uint8_t)input[2];
        const bool second_valid =
            is_continuation(second) &&
            (first != 0xE0U || second >= 0xA0U) &&
            (first != 0xEDU || second <= 0x9FU);

        if (second != 0U && third != 0U && second_valid &&
            is_continuation(third)) {
            character->code_point = ((uint32_t)(first & 0x0FU) << 12U) |
                                    ((uint32_t)(second & 0x3FU) << 6U) |
                                    (uint32_t)(third & 0x3FU);
            character->length = 3U;
            return true;
        }
    }

    if (first >= 0xF0U && first <= 0xF4U && remaining >= 4U) {
        const uint8_t second = (uint8_t)input[1];
        const uint8_t third = (uint8_t)input[2];
        const uint8_t fourth = (uint8_t)input[3];
        const bool second_valid =
            is_continuation(second) &&
            (first != 0xF0U || second >= 0x90U) &&
            (first != 0xF4U || second <= 0x8FU);

        if (second != 0U && third != 0U && fourth != 0U && second_valid &&
            is_continuation(third) && is_continuation(fourth)) {
            character->code_point = ((uint32_t)(first & 0x07U) << 18U) |
                                    ((uint32_t)(second & 0x3FU) << 12U) |
                                    ((uint32_t)(third & 0x3FU) << 6U) |
                                    (uint32_t)(fourth & 0x3FU);
            character->length = 4U;
            return true;
        }
    }

    return false;
}

static bool is_whitespace(uint32_t code_point)
{
    return (code_point >= 0x0009U && code_point <= 0x000DU) ||
           code_point == 0x0020U || code_point == 0x0085U ||
           code_point == 0x00A0U || code_point == 0x1680U ||
           (code_point >= 0x2000U && code_point <= 0x200AU) ||
           code_point == 0x2028U || code_point == 0x2029U ||
           code_point == 0x202FU || code_point == 0x205FU ||
           code_point == 0x3000U;
}

static bool should_remove(uint32_t code_point)
{
    return (code_point >= 0x1F000U && code_point <= 0x1FAFFU) ||
           (code_point >= 0xFE00U && code_point <= 0xFE0FU) ||
           (code_point >= 0xE0100U && code_point <= 0xE01EFU) ||
           code_point == 0x200DU;
}

static bool is_supported_character(uint32_t code_point)
{
    const bool ascii_printable =
        code_point >= 0x0021U && code_point <= 0x007EU;
    const bool cyrillic = code_point >= 0x0400U && code_point <= 0x04FFU;
    // Notification previews commonly use guillemets, smart quotes, dashes,
    // bullets, and ellipses in addition to the display's text glyphs.
    const bool guillemet = code_point == 0x00ABU || code_point == 0x00BBU ||
                           code_point == 0x2039U || code_point == 0x203AU;
    const bool dash = code_point >= 0x2010U && code_point <= 0x2015U;
    const bool quotation_mark =
        code_point >= 0x2018U && code_point <= 0x201FU;
    const bool common_notification_punctuation =
        code_point == 0x2022U || code_point == 0x2026U;

    return ascii_printable || cyrillic || guillemet || dash ||
           quotation_mark || common_notification_punctuation;
}

static void normalize_field(char *field, size_t capacity)
{
    size_t read_index = 0U;
    size_t write_index = 0U;
    bool pending_space = false;

    while (read_index < capacity && field[read_index] != '\0') {
        utf8_character_t character;
        if (!decode_utf8(&field[read_index], capacity - read_index,
                         &character)) {
            ++read_index;
            continue;
        }

        read_index += character.length;
        if (is_whitespace(character.code_point)) {
            pending_space = write_index > 0U;
            continue;
        }
        if (should_remove(character.code_point)) {
            continue;
        }
        if (!is_supported_character(character.code_point)) {
            continue;
        }

        const size_t required = character.length + (pending_space ? 1U : 0U);
        if (required > (capacity - 1U) - write_index) {
            break;
        }
        if (pending_space) {
            field[write_index++] = ' ';
            pending_space = false;
        }
        const size_t source_index = read_index - character.length;
        for (size_t offset = 0U; offset < character.length; ++offset) {
            field[write_index++] = field[source_index + offset];
        }
    }

    field[write_index] = '\0';
}

bool notification_is_telegram(const notification_t *item)
{
    if (item == NULL) {
        return false;
    }

    return memcmp(item->app_id, TELEGRAM_IOS_BUNDLE_ID,
                  sizeof(TELEGRAM_IOS_BUNDLE_ID)) == 0;
}

void notification_normalize(notification_t *item)
{
    if (item == NULL) {
        return;
    }

    item->app_id[sizeof(item->app_id) - 1U] = '\0';
    normalize_field(item->title, sizeof(item->title));
    normalize_field(item->subtitle, sizeof(item->subtitle));
    normalize_field(item->message, sizeof(item->message));
}
