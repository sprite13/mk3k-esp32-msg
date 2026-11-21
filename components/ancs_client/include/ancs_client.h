#pragma once

#include "esp_err.h"
#include "notification.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ANCS_CLIENT_PAIRING,
    ANCS_CLIENT_CONNECTED,
    ANCS_CLIENT_RECONNECTING,
    ANCS_CLIENT_ERROR
} ancs_client_state_t;

typedef void (*ancs_notification_cb_t)(const notification_t *notification, void *ctx);
typedef void (*ancs_state_cb_t)(ancs_client_state_t state, void *ctx);

typedef struct {
    ancs_notification_cb_t on_notification;
    ancs_state_cb_t on_state;
    void *ctx;
} ancs_client_config_t;

/*
 * Callbacks run synchronously on the NimBLE host task. The notification pointer
 * remains valid only for the duration of the callback and must be copied before
 * it is retained or passed to another task.
 */
esp_err_t ancs_client_start(const ancs_client_config_t *config);
esp_err_t ancs_client_forget_bond(void);

#ifdef __cplusplus
}
#endif
