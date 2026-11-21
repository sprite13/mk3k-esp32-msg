#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "ancs_client.h"
#include "app_state.h"
#include "board.h"
#include "display_ui.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

enum {
    APP_EVENT_QUEUE_CAPACITY = 8,
    APP_TASK_STACK_SIZE = 8192,
    APP_TASK_PRIORITY = 5,
    APP_BUTTON_POLL_MS = 20,
    APP_NOTIFICATION_BEEP_HZ = 2200,
    APP_NOTIFICATION_BEEP_MS = 50,
    APP_NO_PENDING_CONNECTION = -1,
};

static const char *const TAG = "app";
static QueueHandle_t s_event_queue;
static atomic_int s_pending_connection =
    ATOMIC_VAR_INIT(APP_NO_PENDING_CONNECTION);
static atomic_uint s_dropped_notifications = ATOMIC_VAR_INIT(0U);
static atomic_uint s_deferred_connections = ATOMIC_VAR_INIT(0U);

static app_connection_state_t map_connection_state(ancs_client_state_t state)
{
    switch (state) {
    case ANCS_CLIENT_PAIRING:
        return APP_CONNECTION_PAIRING;
    case ANCS_CLIENT_CONNECTED:
        return APP_CONNECTION_CONNECTED;
    case ANCS_CLIENT_RECONNECTING:
        return APP_CONNECTION_RECONNECTING;
    case ANCS_CLIENT_ERROR:
        return APP_CONNECTION_ERROR;
    }
    return APP_CONNECTION_ERROR;
}

static void notification_callback(const notification_t *notification, void *ctx)
{
    (void)ctx;
    if (notification == NULL || s_event_queue == NULL) {
        return;
    }

    app_event_t event = {.type = APP_EVENT_NOTIFICATION};
    event.data.notification = *notification;
    if (xQueueSendToBack(s_event_queue, &event, 0U) != pdPASS) {
        (void)atomic_fetch_add_explicit(&s_dropped_notifications, 1U,
                                        memory_order_relaxed);
    }
}

static void publish_connection_state(app_connection_state_t state)
{
    atomic_store_explicit(&s_pending_connection, (int)state,
                          memory_order_release);
    if (s_event_queue == NULL) {
        return;
    }

    const app_event_t event = {
        .type = APP_EVENT_CONNECTION_STATE,
        .data.connection = state,
    };
    if (xQueueSendToBack(s_event_queue, &event, 0U) != pdPASS) {
        (void)atomic_fetch_add_explicit(&s_deferred_connections, 1U,
                                        memory_order_relaxed);
    }
}

static void state_callback(ancs_client_state_t state, void *ctx)
{
    (void)ctx;
    publish_connection_state(map_connection_state(state));
}

static ui_connection_state_t map_ui_connection(app_connection_state_t state)
{
    switch (state) {
    case APP_CONNECTION_PAIRING:
        return UI_PAIRING;
    case APP_CONNECTION_CONNECTED:
        return UI_CONNECTED;
    case APP_CONNECTION_RECONNECTING:
        return UI_RECONNECTING;
    case APP_CONNECTION_ERROR:
        return UI_ERROR;
    }
    return UI_ERROR;
}

static void render_state(const app_state_t *state)
{
    const size_t total = notification_queue_size(&state->queue);
    const display_ui_model_t model = {
        .connection = map_ui_connection(state->connection),
        .battery_percent = app_battery_percent(board_battery_millivolts()),
        .position = total == 0U ? 0U : state->queue.selected_offset + 1U,
        .total = total,
        .notification = notification_queue_current(&state->queue),
    };
    display_ui_render(&model);
    board_backlight_set(true);
}

static bool event_should_render(const app_event_t *event,
                                const app_state_t *state,
                                bool forget_bond_requested)
{
    switch (event->type) {
    case APP_EVENT_NOTIFICATION:
        return state->notification_accepted;
    case APP_EVENT_BUTTON_A:
    case APP_EVENT_BUTTON_B_DOWN:
    case APP_EVENT_BUTTON_B_UP:
    case APP_EVENT_CONNECTION_STATE:
        return true;
    case APP_EVENT_TIMER:
        return forget_bond_requested;
    }
    return false;
}

static void apply_event(app_state_t *state, const app_event_t *event,
                        int64_t now_us)
{
    const bool was_awake = state->display_awake;
    app_state_reduce(state, event, now_us);
    const bool forget_bond_requested =
        app_state_take_forget_bond_request(state);

    if (was_awake && !state->display_awake) {
        board_backlight_set(false);
        display_ui_sleep();
    }
    if (state->display_awake &&
        event_should_render(event, state, forget_bond_requested)) {
        render_state(state);
    }
    if (state->notification_accepted) {
        board_beep(APP_NOTIFICATION_BEEP_HZ, APP_NOTIFICATION_BEEP_MS);
    }
    if (forget_bond_requested) {
        const esp_err_t err = ancs_client_forget_bond();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "failed to request bond reset: %s",
                     esp_err_to_name(err));
        }
    }
}

static void apply_pending_connection(app_state_t *state)
{
    const int pending = atomic_exchange_explicit(
        &s_pending_connection, APP_NO_PENDING_CONNECTION, memory_order_acq_rel);
    if (pending == APP_NO_PENDING_CONNECTION) {
        return;
    }

    const app_event_t event = {
        .type = APP_EVENT_CONNECTION_STATE,
        .data.connection = (app_connection_state_t)pending,
    };
    apply_event(state, &event, esp_timer_get_time());
}

static void report_event_overflow(void)
{
    const unsigned int dropped = atomic_exchange_explicit(
        &s_dropped_notifications, 0U, memory_order_relaxed);
    const unsigned int deferred = atomic_exchange_explicit(
        &s_deferred_connections, 0U, memory_order_relaxed);
    if (dropped > 0U) {
        ESP_LOGW(TAG, "event queue full: dropped %u notification event(s)",
                 dropped);
    }
    if (deferred > 0U) {
        ESP_LOGW(TAG,
                 "event queue full: deferred %u connection update(s); latest preserved",
                 deferred);
    }
}

static void process_queued_event(app_state_t *state, const app_event_t *event)
{
    if (event->type == APP_EVENT_CONNECTION_STATE) {
        apply_pending_connection(state);
        return;
    }
    apply_event(state, event, esp_timer_get_time());
}

static void poll_buttons(app_state_t *state, board_buttons_t *previous)
{
    const board_buttons_t current = board_buttons_read();
    const int64_t now_us = esp_timer_get_time();

    if (current.a_down && !previous->a_down) {
        const app_event_t event = {.type = APP_EVENT_BUTTON_A};
        apply_event(state, &event, now_us);
    }
    if (current.b_down != previous->b_down) {
        const app_event_t event = {
            .type = current.b_down ? APP_EVENT_BUTTON_B_DOWN :
                                            APP_EVENT_BUTTON_B_UP,
        };
        apply_event(state, &event, now_us);
    }
    *previous = current;
}

static void app_task(void *arg)
{
    (void)arg;
    app_state_t state;
    app_state_init(&state);
    board_buttons_t previous_buttons = board_buttons_read();
    const TickType_t poll_ticks = pdMS_TO_TICKS(APP_BUTTON_POLL_MS);

    while (true) {
        apply_pending_connection(&state);
        report_event_overflow();

        app_event_t event;
        if (xQueueReceive(s_event_queue, &event, poll_ticks) == pdTRUE) {
            process_queued_event(&state, &event);
            int drained = 1;
            while (drained < APP_EVENT_QUEUE_CAPACITY &&
                   xQueueReceive(s_event_queue, &event, 0U) == pdTRUE) {
                process_queued_event(&state, &event);
                ++drained;
            }
        }

        apply_pending_connection(&state);
        poll_buttons(&state, &previous_buttons);
        const app_event_t timer_event = {.type = APP_EVENT_TIMER};
        apply_event(&state, &timer_event, esp_timer_get_time());
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "m5stickc-telegram-ancs boot");

    const esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err != ESP_OK) {
        ESP_LOGE(TAG, "NVS initialization failed; existing data preserved: %s",
                 esp_err_to_name(nvs_err));
        return;
    }

    board_init();
    const esp_err_t display_err = display_ui_init();
    if (display_err != ESP_OK) {
        ESP_LOGE(TAG, "display initialization failed: %s",
                 esp_err_to_name(display_err));
        return;
    }

    s_event_queue = xQueueCreate(APP_EVENT_QUEUE_CAPACITY, sizeof(app_event_t));
    if (s_event_queue == NULL) {
        ESP_LOGE(TAG, "failed to create application event queue");
        return;
    }

    const BaseType_t task_created =
        xTaskCreate(app_task, "app", APP_TASK_STACK_SIZE, NULL,
                    APP_TASK_PRIORITY, NULL);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "failed to create application task");
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
        return;
    }

    const ancs_client_config_t ancs_config = {
        .on_notification = notification_callback,
        .on_state = state_callback,
        .ctx = NULL,
    };
    const esp_err_t ancs_err = ancs_client_start(&ancs_config);
    if (ancs_err != ESP_OK) {
        ESP_LOGE(TAG, "ANCS initialization failed: %s",
                 esp_err_to_name(ancs_err));
        publish_connection_state(APP_CONNECTION_ERROR);
    }
}
