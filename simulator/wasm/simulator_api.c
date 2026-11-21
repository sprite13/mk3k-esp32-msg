#include "simulator_api.h"

#include <stddef.h>
#include <string.h>

#include "app_screen.h"
#include "app_state.h"
#include "notification.h"
#include "notification_normalizer.h"
#include "display_layout.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define SIMULATOR_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define SIMULATOR_EXPORT
#endif

_Static_assert(sizeof(((notification_t *)0)->app_id) ==
                   SIM_NOTIFICATION_APP_ID_CAPACITY,
               "core and simulator app-id capacities differ");
_Static_assert(sizeof(((notification_t *)0)->title) ==
                   SIM_NOTIFICATION_TITLE_CAPACITY,
               "core and simulator title capacities differ");
_Static_assert(sizeof(((notification_t *)0)->subtitle) ==
                   SIM_NOTIFICATION_SUBTITLE_CAPACITY,
               "core and simulator subtitle capacities differ");
_Static_assert(sizeof(((notification_t *)0)->message) ==
                   SIM_NOTIFICATION_MESSAGE_CAPACITY,
               "core and simulator message capacities differ");
_Static_assert(SIM_DISPLAY_MAX_COLUMNS == DISPLAY_LAYOUT_MAX_COLUMNS,
               "firmware and simulator display columns differ");
_Static_assert(SIM_DISPLAY_MAX_LINES == DISPLAY_LAYOUT_MAX_LINES,
               "firmware and simulator display lines differ");
_Static_assert(SIM_DISPLAY_FONT_WIDTH == DISPLAY_FONT_WIDTH,
               "firmware and simulator font widths differ");
_Static_assert(SIM_DISPLAY_FONT_HEIGHT == DISPLAY_FONT_HEIGHT,
               "firmware and simulator font heights differ");
_Static_assert((int)SIM_DISPLAY_LINE_STATUS == (int)DISPLAY_LINE_STATUS &&
                   (int)SIM_DISPLAY_LINE_WARNING == (int)DISPLAY_LINE_WARNING &&
                   (int)SIM_DISPLAY_LINE_TITLE == (int)DISPLAY_LINE_TITLE &&
                   (int)SIM_DISPLAY_LINE_SUBTITLE == (int)DISPLAY_LINE_SUBTITLE &&
                   (int)SIM_DISPLAY_LINE_BODY == (int)DISPLAY_LINE_BODY,
               "firmware and simulator display styles differ");

typedef struct {
    app_state_t app;
    int64_t last_now_us;
    uint32_t next_uid;
    bool previews_hidden;
    bool beep_requested;
    bool initialized;
} simulator_instance_t;

static simulator_instance_t simulator;

static int32_t require_initialized(void)
{
    return simulator.initialized ? SIM_OK : SIM_ERR_NOT_INITIALIZED;
}

static int32_t validate_timestamp(int64_t now_us)
{
    if (now_us < 0 || now_us > INT64_MAX - APP_DISPLAY_TIMEOUT_US ||
        (simulator.initialized && now_us < simulator.last_now_us)) {
        return SIM_ERR_INVALID_TIMESTAMP;
    }
    return SIM_OK;
}

static int32_t reduce_event(const app_event_t *event, int64_t now_us)
{
    const int32_t initialized = require_initialized();
    if (initialized != SIM_OK) {
        return initialized;
    }
    const int32_t validation = validate_timestamp(now_us);
    if (validation != SIM_OK) {
        return validation;
    }
    app_state_reduce(&simulator.app, event, now_us);
    simulator.last_now_us = now_us;
    return SIM_OK;
}

static void copy_notification(simulator_notification_t *destination,
                              const notification_t *source)
{
    destination->uid = source->uid;
    memcpy(destination->app_id, source->app_id, sizeof(destination->app_id));
    memcpy(destination->title, source->title, sizeof(destination->title));
    memcpy(destination->subtitle, source->subtitle,
           sizeof(destination->subtitle));
    memcpy(destination->message, source->message,
           sizeof(destination->message));
}

static void copy_string(char *destination, size_t capacity,
                        const char *source)
{
    size_t index = 0U;
    while (index + 1U < capacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

SIMULATOR_EXPORT uint32_t simulator_abi_version(void)
{
    return SIMULATOR_ABI_VERSION;
}

SIMULATOR_EXPORT int32_t simulator_reset(int64_t now_us)
{
    if (now_us < 0 || now_us > INT64_MAX - APP_DISPLAY_TIMEOUT_US) {
        return SIM_ERR_INVALID_TIMESTAMP;
    }
    memset(&simulator, 0, sizeof(simulator));
    app_state_init(&simulator.app);
    simulator.last_now_us = now_us;
    simulator.next_uid = 1U;
    simulator.initialized = true;
    return SIM_OK;
}

SIMULATOR_EXPORT int32_t simulator_tick(int64_t now_us)
{
    const app_event_t event = {.type = APP_EVENT_TIMER};
    return reduce_event(&event, now_us);
}

SIMULATOR_EXPORT int32_t
simulator_send_notification(const char *title, const char *subtitle,
                            const char *message, int64_t now_us)
{
    const int32_t initialized = require_initialized();
    if (initialized != SIM_OK) {
        return initialized;
    }
    if (title == NULL || subtitle == NULL || message == NULL) {
        return SIM_ERR_NULL_ARGUMENT;
    }
    const int32_t validation = validate_timestamp(now_us);
    if (validation != SIM_OK) {
        return validation;
    }
    if (simulator.app.connection != APP_CONNECTION_CONNECTED) {
        return SIM_ERR_DISCONNECTED;
    }

    app_event_t event = {.type = APP_EVENT_NOTIFICATION};
    event.data.notification.uid = simulator.next_uid;
    copy_string(event.data.notification.app_id,
                sizeof(event.data.notification.app_id),
                TELEGRAM_IOS_BUNDLE_ID);
    if (!simulator.previews_hidden) {
        copy_string(event.data.notification.title,
                    sizeof(event.data.notification.title), title);
        copy_string(event.data.notification.subtitle,
                    sizeof(event.data.notification.subtitle), subtitle);
        copy_string(event.data.notification.message,
                    sizeof(event.data.notification.message), message);
    }

    app_state_reduce(&simulator.app, &event, now_us);
    simulator.last_now_us = now_us;
    if (!simulator.app.notification_accepted) {
        return SIM_ERR_INVALID_ARGUMENT;
    }
    ++simulator.next_uid;
    simulator.beep_requested = true;
    return SIM_OK;
}

SIMULATOR_EXPORT int32_t simulator_button_a(int64_t now_us)
{
    const app_event_t event = {.type = APP_EVENT_BUTTON_A};
    return reduce_event(&event, now_us);
}

SIMULATOR_EXPORT int32_t simulator_button_b_down(int64_t now_us)
{
    const app_event_t event = {.type = APP_EVENT_BUTTON_B_DOWN};
    return reduce_event(&event, now_us);
}

SIMULATOR_EXPORT int32_t simulator_button_b_up(int64_t now_us)
{
    const app_event_t event = {.type = APP_EVENT_BUTTON_B_UP};
    return reduce_event(&event, now_us);
}

SIMULATOR_EXPORT int32_t
simulator_set_connection(uint8_t connection, int64_t now_us)
{
    const int32_t initialized = require_initialized();
    if (initialized != SIM_OK) {
        return initialized;
    }
    if (connection > SIM_CONNECTION_ERROR) {
        return SIM_ERR_INVALID_ARGUMENT;
    }
    const app_event_t event = {
        .type = APP_EVENT_CONNECTION_STATE,
        .data.connection = (app_connection_state_t)connection,
    };
    return reduce_event(&event, now_us);
}

SIMULATOR_EXPORT int32_t simulator_set_battery(uint8_t percent,
                                               int64_t now_us)
{
    const int32_t initialized = require_initialized();
    if (initialized != SIM_OK) {
        return initialized;
    }
    if (percent > 100U && percent != SIM_BATTERY_PERCENT_UNKNOWN) {
        return SIM_ERR_INVALID_ARGUMENT;
    }
    const app_event_t event = {
        .type = APP_EVENT_BATTERY_PERCENT,
        .data.battery_percent = percent,
    };
    return reduce_event(&event, now_us);
}

SIMULATOR_EXPORT int32_t simulator_set_previews_hidden(bool hidden)
{
    const int32_t initialized = require_initialized();
    if (initialized != SIM_OK) {
        return initialized;
    }
    simulator.previews_hidden = hidden;
    return SIM_OK;
}

SIMULATOR_EXPORT int32_t simulator_take_beep(void)
{
    const int32_t initialized = require_initialized();
    if (initialized != SIM_OK) {
        return initialized;
    }
    const int32_t requested = simulator.beep_requested ? 1 : 0;
    simulator.beep_requested = false;
    return requested;
}

SIMULATOR_EXPORT int32_t simulator_snapshot(simulator_snapshot_t *out)
{
    const int32_t initialized = require_initialized();
    if (initialized != SIM_OK) {
        return initialized;
    }
    if (out == NULL) {
        return SIM_ERR_NULL_ARGUMENT;
    }

    app_screen_t screen;
    app_screen_make(&simulator.app, &screen);
    memset(out, 0, sizeof(*out));
    out->abi_version = SIMULATOR_ABI_VERSION;
    out->connection = (uint8_t)screen.connection;
    out->battery_percent = screen.battery_percent;
    out->position = (uint8_t)screen.position;
    out->total = (uint8_t)screen.total;
    out->display_awake = screen.display_awake ? 1U : 0U;
    out->notification_present = screen.notification_present ? 1U : 0U;
    out->beep_requested = simulator.beep_requested ? 1U : 0U;
    copy_notification(&out->notification, &screen.notification);
    return SIM_OK;
}

static display_connection_t display_connection(app_connection_state_t state)
{
    switch (state) {
    case APP_CONNECTION_PAIRING:
        return DISPLAY_CONNECTION_PAIRING;
    case APP_CONNECTION_CONNECTED:
        return DISPLAY_CONNECTION_CONNECTED;
    case APP_CONNECTION_RECONNECTING:
        return DISPLAY_CONNECTION_RECONNECTING;
    case APP_CONNECTION_ERROR:
    default:
        return DISPLAY_CONNECTION_ERROR;
    }
}

static display_bond_reset_state_t display_bond_reset(
    app_bond_reset_state_t state)
{
    switch (state) {
    case APP_BOND_RESET_HOLDING:
        return DISPLAY_BOND_RESET_HOLDING;
    case APP_BOND_RESET_REQUESTED:
    case APP_BOND_RESET_SUBMITTED:
        return DISPLAY_BOND_RESET_REQUESTED;
    case APP_BOND_RESET_COMPLETED:
        return DISPLAY_BOND_RESET_COMPLETED;
    case APP_BOND_RESET_FAILED:
        return DISPLAY_BOND_RESET_FAILED;
    case APP_BOND_RESET_IDLE:
    default:
        return DISPLAY_BOND_RESET_NONE;
    }
}

SIMULATOR_EXPORT int32_t
simulator_display_plan(simulator_display_plan_t *out)
{
    const int32_t initialized = require_initialized();
    if (initialized != SIM_OK) {
        return initialized;
    }
    if (out == NULL) {
        return SIM_ERR_NULL_ARGUMENT;
    }

    app_screen_t screen;
    display_render_plan_t source;
    app_screen_make(&simulator.app, &screen);
    display_render_plan_make(
        display_connection(screen.connection), screen.battery_percent,
        screen.position, screen.total,
        screen.notification_present ? &screen.notification : NULL,
        display_bond_reset(screen.bond_reset_state),
        screen.bond_reset_progress_percent, &source);

    memset(out, 0, sizeof(*out));
    for (size_t line = 0U; line < SIM_DISPLAY_MAX_LINES; ++line) {
        out->lines[line].length = (uint8_t)source.lines[line].length;
        out->lines[line].style = (uint8_t)source.lines[line].style;
        memcpy(out->lines[line].glyphs, source.lines[line].glyphs,
               sizeof(out->lines[line].glyphs));
    }
    return SIM_OK;
}

SIMULATOR_EXPORT int32_t
simulator_font_glyph(uint32_t codepoint, simulator_font_glyph_t *out)
{
    const int32_t initialized = require_initialized();
    if (initialized != SIM_OK) {
        return initialized;
    }
    if (out == NULL) {
        return SIM_ERR_NULL_ARGUMENT;
    }
    const uint16_t *rows = NULL;
    if (!display_font_get_glyph(codepoint, &rows)) {
        return SIM_ERR_INVALID_ARGUMENT;
    }
    memcpy(out->rows, rows, sizeof(out->rows));
    return SIM_OK;
}
