#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "notification.h"

#define SIMULATOR_ABI_VERSION UINT32_C(1)
#define SIM_BATTERY_PERCENT_UNKNOWN UINT8_MAX

typedef enum {
    SIM_OK = 0,
    SIM_ERR_NULL_ARGUMENT = -1,
    SIM_ERR_INVALID_ARGUMENT = -2,
    SIM_ERR_INVALID_TIMESTAMP = -3,
    SIM_ERR_DISCONNECTED = -4,
} simulator_result_t;

typedef enum {
    SIM_CONNECTION_PAIRING = 0,
    SIM_CONNECTION_CONNECTED = 1,
    SIM_CONNECTION_RECONNECTING = 2,
    SIM_CONNECTION_ERROR = 3,
} simulator_connection_t;

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
    notification_t notification;
} simulator_snapshot_t;

uint32_t simulator_abi_version(void);
int32_t simulator_reset(int64_t now_us);
int32_t simulator_tick(int64_t now_us);
int32_t simulator_send_notification(const char *title, const char *subtitle,
                                    const char *message, int64_t now_us);
int32_t simulator_button_a(int64_t now_us);
int32_t simulator_button_b_down(int64_t now_us);
int32_t simulator_button_b_up(int64_t now_us);
int32_t simulator_set_connection(uint8_t connection, int64_t now_us);
int32_t simulator_set_battery(uint8_t percent, int64_t now_us);
int32_t simulator_set_previews_hidden(bool hidden);
uint8_t simulator_take_beep(void);
int32_t simulator_snapshot(simulator_snapshot_t *out);
