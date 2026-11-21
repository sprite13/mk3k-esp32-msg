#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "simulator_api.h"

typedef enum {
    ACTION_RESET,
    ACTION_CONNECT,
    ACTION_NOTIFY,
    ACTION_TICK,
    ACTION_BUTTON_A,
    ACTION_BUTTON_B_DOWN,
    ACTION_BUTTON_B_UP,
    ACTION_BATTERY,
    ACTION_PREVIEWS_HIDDEN,
} action_kind_t;

typedef struct {
    action_kind_t kind;
    int64_t at;
    uint8_t percent;
    bool hidden;
    const char *title;
    const char *subtitle;
    const char *message;
} scenario_action_t;

typedef struct {
    const char *name;
    const scenario_action_t *actions;
    size_t action_count;
} scenario_t;

#include "scenarios.generated.h"

static void print_json_string(const char *value)
{
    putchar('"');
    for (const unsigned char *cursor = (const unsigned char *)value;
         *cursor != '\0'; ++cursor) {
        if (*cursor == '"' || *cursor == '\\') {
            putchar('\\');
            putchar((int)*cursor);
        } else if (*cursor < 0x20U) {
            printf("\\u%04x", (unsigned int)*cursor);
        } else {
            putchar((int)*cursor);
        }
    }
    putchar('"');
}

static int32_t run_action(const scenario_action_t *action)
{
    switch (action->kind) {
    case ACTION_RESET:
        return simulator_reset(action->at);
    case ACTION_CONNECT:
        return simulator_set_connection(SIM_CONNECTION_CONNECTED, action->at);
    case ACTION_NOTIFY:
        return simulator_send_notification(action->title, action->subtitle,
                                           action->message, action->at);
    case ACTION_TICK:
        return simulator_tick(action->at);
    case ACTION_BUTTON_A:
        return simulator_button_a(action->at);
    case ACTION_BUTTON_B_DOWN:
        return simulator_button_b_down(action->at);
    case ACTION_BUTTON_B_UP:
        return simulator_button_b_up(action->at);
    case ACTION_BATTERY:
        return simulator_set_battery(action->percent, action->at);
    case ACTION_PREVIEWS_HIDDEN:
        return simulator_set_previews_hidden(action->hidden);
    }
    return SIM_ERR_INVALID_ARGUMENT;
}

static void print_snapshot(const simulator_snapshot_t *snapshot)
{
    printf("{\"abiVersion\":%" PRIu32 ",\"connection\":%u,"
           "\"batteryPercent\":%u,\"position\":%u,\"total\":%u,"
           "\"displayAwake\":%u,\"notificationPresent\":%u,"
           "\"beepRequested\":%u,\"notification\":{\"uid\":%" PRIu32 ","
           "\"appId\":",
           snapshot->abi_version, (unsigned int)snapshot->connection,
           (unsigned int)snapshot->battery_percent,
           (unsigned int)snapshot->position, (unsigned int)snapshot->total,
           (unsigned int)snapshot->display_awake,
           (unsigned int)snapshot->notification_present,
           (unsigned int)snapshot->beep_requested, snapshot->notification.uid);
    print_json_string(snapshot->notification.app_id);
    fputs(",\"title\":", stdout);
    print_json_string(snapshot->notification.title);
    fputs(",\"subtitle\":", stdout);
    print_json_string(snapshot->notification.subtitle);
    fputs(",\"message\":", stdout);
    print_json_string(snapshot->notification.message);
    fputs("}}", stdout);
}

int main(void)
{
    putchar('[');
    for (size_t scenario_index = 0U; scenario_index < scenario_count;
         ++scenario_index) {
        const scenario_t *scenario = &scenarios[scenario_index];
        if (scenario_index != 0U) {
            putchar(',');
        }
        fputs("{\"name\":", stdout);
        print_json_string(scenario->name);
        fputs(",\"steps\":[", stdout);
        for (size_t action_index = 0U; action_index < scenario->action_count;
             ++action_index) {
            if (action_index != 0U) {
                putchar(',');
            }
            const int32_t result = run_action(&scenario->actions[action_index]);
            simulator_snapshot_t snapshot;
            const int32_t snapshot_result = simulator_snapshot(&snapshot);
            if (snapshot_result != SIM_OK) {
                fprintf(stderr, "snapshot failed: %" PRId32 "\n", snapshot_result);
                return 1;
            }
            printf("{\"result\":%" PRId32 ",\"snapshot\":", result);
            print_snapshot(&snapshot);
            putchar('}');
        }
        fputs("]}", stdout);
    }
    fputs("]\n", stdout);
    return 0;
}
