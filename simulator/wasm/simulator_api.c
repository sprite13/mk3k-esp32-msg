#include "simulator_api.h"

#include <stddef.h>
#include <string.h>

#include "app_screen.h"
#include "app_state.h"
#include "notification_normalizer.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define SIMULATOR_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define SIMULATOR_EXPORT
#endif

typedef struct {
    app_state_t app;
    int64_t last_now_us;
    uint32_t next_uid;
    bool previews_hidden;
    bool beep_requested;
    bool initialized;
} simulator_instance_t;

static simulator_instance_t simulator;

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
    const int32_t validation = validate_timestamp(now_us);
    if (validation != SIM_OK) {
        return validation;
    }
    app_state_reduce(&simulator.app, event, now_us);
    simulator.last_now_us = now_us;
    return SIM_OK;
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
    if (percent > 100U) {
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
    simulator.previews_hidden = hidden;
    return SIM_OK;
}

SIMULATOR_EXPORT uint8_t simulator_take_beep(void)
{
    const uint8_t requested = simulator.beep_requested ? 1U : 0U;
    simulator.beep_requested = false;
    return requested;
}

SIMULATOR_EXPORT int32_t simulator_snapshot(simulator_snapshot_t *out)
{
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
    out->notification = screen.notification;
    return SIM_OK;
}
