#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "ancs_client.h"
#include "app_notification_ring.h"
#include "app_state.h"
#include "board.h"
#include "display_ui.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "notification_normalizer.h"
#include "nvs_flash.h"
#include "power_manager.h"
#include "sdkconfig.h"

enum {
    APP_EVENT_QUEUE_CAPACITY = 8,
    APP_TASK_STACK_SIZE = 8192,
    APP_TASK_PRIORITY = 5,
    APP_BUTTON_POLL_MS = 20,
    APP_NOTIFICATION_BEEP_HZ = 2200,
    APP_NOTIFICATION_BEEP_MS = 50,
    APP_BOND_PROGRESS_RENDER_STEP_PERCENT = 10,
    APP_NO_PENDING_CONNECTION = -1,
    APP_NO_PENDING_BOND_RESULT = -1,
};

static const char *const TAG = "app";
static app_notification_ring_t s_notification_ring;
static TaskHandle_t s_app_task;
static atomic_int s_pending_connection =
    ATOMIC_VAR_INIT(APP_NO_PENDING_CONNECTION);
static atomic_int s_pending_bond_result =
    ATOMIC_VAR_INIT(APP_NO_PENDING_BOND_RESULT);
static atomic_uint s_evicted_notifications = ATOMIC_VAR_INIT(0U);

static void wake_app_task(void)
{
    if (s_app_task != NULL) {
        xTaskNotifyGive(s_app_task);
    }
}

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
    if (notification == NULL || !notification_is_telegram(notification)) {
        return;
    }

    const app_notification_ring_result_t result =
        app_notification_ring_push(&s_notification_ring, notification);
    if (result == APP_NOTIFICATION_RING_REJECTED) {
        return;
    }
    if (result == APP_NOTIFICATION_RING_REPLACED_OLDEST) {
        (void)atomic_fetch_add_explicit(&s_evicted_notifications, 1U,
                                        memory_order_relaxed);
    }
    wake_app_task();
}

static void publish_connection_state(app_connection_state_t state)
{
    atomic_store_explicit(&s_pending_connection, (int)state,
                          memory_order_release);
    wake_app_task();
}

static void state_callback(ancs_client_state_t state, void *ctx)
{
    (void)ctx;
    publish_connection_state(map_connection_state(state));
}

static void bond_reset_callback(ancs_bond_reset_result_t result, void *ctx)
{
    (void)ctx;
    atomic_store_explicit(&s_pending_bond_result, (int)result,
                          memory_order_release);
    wake_app_task();
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

static ui_bond_reset_state_t map_ui_bond_reset(app_bond_reset_state_t state)
{
    switch (state) {
    case APP_BOND_RESET_HOLDING:
        return UI_BOND_RESET_HOLDING;
    case APP_BOND_RESET_REQUESTED:
    case APP_BOND_RESET_SUBMITTED:
        return UI_BOND_RESET_REQUESTED;
    case APP_BOND_RESET_COMPLETED:
        return UI_BOND_RESET_COMPLETED;
    case APP_BOND_RESET_FAILED:
        return UI_BOND_RESET_FAILED;
    case APP_BOND_RESET_IDLE:
    default:
        return UI_BOND_RESET_NONE;
    }
}

static bool render_state(const app_state_t *state)
{
    const size_t total = notification_queue_size(&state->queue);
    const display_ui_model_t model = {
        .connection = map_ui_connection(state->connection),
        .battery_percent = app_battery_percent(board_battery_millivolts()),
        .position = total == 0U ? 0U : state->queue.selected_offset + 1U,
        .total = total,
        .notification = notification_queue_current(&state->queue),
        .bond_reset_state = map_ui_bond_reset(state->bond_reset_state),
        .bond_reset_progress_percent = state->bond_reset_progress_percent,
    };
    display_ui_render(&model);
    const bool healthy = display_ui_is_healthy();
    board_backlight_set(healthy);
    return healthy;
}

static bool event_should_render(const app_event_t *event,
                                const app_state_t *state,
                                bool forget_bond_requested,
                                uint8_t previous_progress_percent,
                                app_bond_reset_state_t previous_reset_state)
{
    switch (event->type) {
    case APP_EVENT_NOTIFICATION:
        return state->notification_accepted;
    case APP_EVENT_BUTTON_A:
    case APP_EVENT_BUTTON_B_DOWN:
    case APP_EVENT_BUTTON_B_UP:
    case APP_EVENT_CONNECTION_STATE:
    case APP_EVENT_BOND_RESET_RESULT:
        return true;
    case APP_EVENT_TIMER:
        return forget_bond_requested ||
               previous_reset_state != state->bond_reset_state ||
               (state->bond_reset_progress_visible &&
                previous_progress_percent /
                        APP_BOND_PROGRESS_RENDER_STEP_PERCENT !=
                    state->bond_reset_progress_percent /
                        APP_BOND_PROGRESS_RENDER_STEP_PERCENT);
    case APP_EVENT_DISPLAY_FAILURE:
        return false;
    }
    return false;
}

static void apply_event(app_state_t *state, const app_event_t *event,
                        int64_t now_us)
{
    const bool was_awake = state->display_awake;
    const uint8_t previous_progress_percent =
        state->bond_reset_progress_percent;
    const app_bond_reset_state_t previous_reset_state =
        state->bond_reset_state;
    app_state_reduce(state, event, now_us);
    const bool forget_bond_requested =
        app_state_take_forget_bond_request(state);

    if (was_awake && !state->display_awake) {
        board_backlight_set(false);
        display_ui_sleep();
    }
    if (state->display_awake &&
        event_should_render(event, state, forget_bond_requested,
                            previous_progress_percent,
                            previous_reset_state)) {
        if (!render_state(state)) {
            const app_event_t failed = {.type = APP_EVENT_DISPLAY_FAILURE};
            app_state_reduce(state, &failed, now_us);
        }
    }
    if (state->notification_accepted) {
        board_beep(APP_NOTIFICATION_BEEP_HZ, APP_NOTIFICATION_BEEP_MS);
    }
    if (forget_bond_requested) {
        const esp_err_t err = ancs_client_forget_bond();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "failed to request bond reset: %s",
                     esp_err_to_name(err));
            const app_event_t result = {
                .type = APP_EVENT_BOND_RESET_RESULT,
                .data.bond_reset_succeeded = false,
            };
            apply_event(state, &result, esp_timer_get_time());
        }
    }
}

static void apply_pending_bond_result(app_state_t *state)
{
    const int pending = atomic_exchange_explicit(
        &s_pending_bond_result, APP_NO_PENDING_BOND_RESULT,
        memory_order_acq_rel);
    if (pending == APP_NO_PENDING_BOND_RESULT) {
        return;
    }
    const app_event_t event = {
        .type = APP_EVENT_BOND_RESET_RESULT,
        .data.bond_reset_succeeded =
            pending == (int)ANCS_BOND_RESET_SUCCEEDED,
    };
    apply_event(state, &event, esp_timer_get_time());
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
    const unsigned int evicted = atomic_exchange_explicit(
        &s_evicted_notifications, 0U, memory_order_relaxed);
    if (evicted > 0U) {
        ESP_LOGW(TAG,
                 "notification queue full: evicted %u oldest event(s); newest preserved",
                 evicted);
    }
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
        apply_pending_bond_result(&state);
        apply_pending_connection(&state);
        report_event_overflow();

        (void)ulTaskNotifyTake(pdTRUE, poll_ticks);
        notification_t notification;
        int drained = 0;
        while (drained < APP_EVENT_QUEUE_CAPACITY &&
               app_notification_ring_pop(&s_notification_ring,
                                         &notification)) {
            const app_event_t event = {
                .type = APP_EVENT_NOTIFICATION,
                .data.notification = notification,
            };
            apply_event(&state, &event, esp_timer_get_time());
            ++drained;
        }

        apply_pending_bond_result(&state);
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

    const esp_err_t power_err = power_manager_init();
    if (power_err != ESP_OK) {
        ESP_LOGE(TAG, "power management initialization failed; continuing "
                      "without configured DFS: %s",
                 esp_err_to_name(power_err));
    } else {
#ifdef CONFIG_FREERTOS_USE_TICKLESS_IDLE
        ESP_LOGW(TAG, "power management: DFS 80..240 MHz, experimental "
                      "automatic light sleep enabled");
#else
        ESP_LOGI(TAG, "power management: DFS 80..240 MHz, automatic light "
                      "sleep disabled");
#endif
    }

    board_init();
    const esp_err_t display_err = display_ui_init();
    if (display_err != ESP_OK) {
        ESP_LOGE(TAG, "display initialization failed: %s",
                 esp_err_to_name(display_err));
        return;
    }

    if (!app_notification_ring_init(&s_notification_ring,
                                    APP_EVENT_QUEUE_CAPACITY)) {
        ESP_LOGE(TAG, "failed to initialize notification ring");
        return;
    }

    const BaseType_t task_created =
        xTaskCreate(app_task, "app", APP_TASK_STACK_SIZE, NULL,
                    APP_TASK_PRIORITY, &s_app_task);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "failed to create application task");
        s_app_task = NULL;
        return;
    }

    const ancs_client_config_t ancs_config = {
        .on_notification = notification_callback,
        .on_state = state_callback,
        .ctx = NULL,
        .on_bond_reset_result = bond_reset_callback,
    };
    const esp_err_t ancs_err = ancs_client_start(&ancs_config);
    if (ancs_err != ESP_OK) {
        ESP_LOGE(TAG, "ANCS initialization failed: %s",
                 esp_err_to_name(ancs_err));
        publish_connection_state(APP_CONNECTION_ERROR);
    }
}
