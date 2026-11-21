#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SIMULATOR_ABI_VERSION UINT32_C(1)
#define SIM_BATTERY_PERCENT_UNKNOWN UINT8_MAX
#define SIM_NOTIFICATION_APP_ID_CAPACITY 64U
#define SIM_NOTIFICATION_TITLE_CAPACITY 96U
#define SIM_NOTIFICATION_SUBTITLE_CAPACITY 96U
#define SIM_NOTIFICATION_MESSAGE_CAPACITY 256U

/* Any incompatible layout or semantic change to this ABI must bump the
 * version. Fields may not be reordered, resized, or repurposed in place. */

typedef enum {
    SIM_OK = 0,
    SIM_ERR_NULL_ARGUMENT = -1,
    SIM_ERR_INVALID_ARGUMENT = -2,
    SIM_ERR_INVALID_TIMESTAMP = -3,
    SIM_ERR_DISCONNECTED = -4,
    SIM_ERR_NOT_INITIALIZED = -5,
} simulator_result_t;

typedef enum {
    SIM_CONNECTION_PAIRING = 0,
    SIM_CONNECTION_CONNECTED = 1,
    SIM_CONNECTION_RECONNECTING = 2,
    SIM_CONNECTION_ERROR = 3,
} simulator_connection_t;

typedef struct {
    uint32_t uid;
    char app_id[SIM_NOTIFICATION_APP_ID_CAPACITY];
    char title[SIM_NOTIFICATION_TITLE_CAPACITY];
    char subtitle[SIM_NOTIFICATION_SUBTITLE_CAPACITY];
    char message[SIM_NOTIFICATION_MESSAGE_CAPACITY];
} simulator_notification_t;

typedef struct {
    uint32_t abi_version;
    uint8_t connection;
    uint8_t battery_percent;
    uint8_t position;
    uint8_t total;
    uint8_t display_awake;
    uint8_t notification_present;
    uint8_t beep_requested;
    uint8_t reserved;
    simulator_notification_t notification;
} simulator_snapshot_t;

_Static_assert(sizeof(simulator_notification_t) == 516U,
               "simulator notification ABI size changed");
_Static_assert(offsetof(simulator_notification_t, uid) == 0U,
               "simulator notification uid offset changed");
_Static_assert(offsetof(simulator_notification_t, app_id) == 4U,
               "simulator notification app_id offset changed");
_Static_assert(offsetof(simulator_notification_t, title) == 68U,
               "simulator notification title offset changed");
_Static_assert(offsetof(simulator_notification_t, subtitle) == 164U,
               "simulator notification subtitle offset changed");
_Static_assert(offsetof(simulator_notification_t, message) == 260U,
               "simulator notification message offset changed");
_Static_assert(sizeof(simulator_snapshot_t) == 528U,
               "simulator snapshot ABI size changed");
_Static_assert(offsetof(simulator_snapshot_t, abi_version) == 0U,
               "simulator snapshot abi_version offset changed");
_Static_assert(offsetof(simulator_snapshot_t, connection) == 4U,
               "simulator snapshot connection offset changed");
_Static_assert(offsetof(simulator_snapshot_t, battery_percent) == 5U,
               "simulator snapshot battery_percent offset changed");
_Static_assert(offsetof(simulator_snapshot_t, position) == 6U,
               "simulator snapshot position offset changed");
_Static_assert(offsetof(simulator_snapshot_t, total) == 7U,
               "simulator snapshot total offset changed");
_Static_assert(offsetof(simulator_snapshot_t, display_awake) == 8U,
               "simulator snapshot display_awake offset changed");
_Static_assert(offsetof(simulator_snapshot_t, notification_present) == 9U,
               "simulator snapshot notification_present offset changed");
_Static_assert(offsetof(simulator_snapshot_t, beep_requested) == 10U,
               "simulator snapshot beep_requested offset changed");
_Static_assert(offsetof(simulator_snapshot_t, reserved) == 11U,
               "simulator snapshot reserved offset changed");
_Static_assert(offsetof(simulator_snapshot_t, notification) == 12U,
               "simulator snapshot notification offset changed");

uint32_t simulator_abi_version(void);
int32_t simulator_reset(int64_t now_us);
int32_t simulator_tick(int64_t now_us);
/* Each input string must point to a NUL-terminated byte sequence. Strings are
 * copied into fixed buffers and normalized as UTF-8 before this call returns. */
int32_t simulator_send_notification(const char *title, const char *subtitle,
                                    const char *message, int64_t now_us);
int32_t simulator_button_a(int64_t now_us);
int32_t simulator_button_b_down(int64_t now_us);
int32_t simulator_button_b_up(int64_t now_us);
int32_t simulator_set_connection(uint8_t connection, int64_t now_us);
int32_t simulator_set_battery(uint8_t percent, int64_t now_us);
int32_t simulator_set_previews_hidden(bool hidden);
int32_t simulator_take_beep(void);
int32_t simulator_snapshot(simulator_snapshot_t *out);
