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
typedef enum {
    ANCS_BOND_RESET_SUCCEEDED,
    ANCS_BOND_RESET_FAILED,
} ancs_bond_reset_result_t;
typedef void (*ancs_bond_reset_cb_t)(ancs_bond_reset_result_t result,
                                     void *ctx);

typedef struct {
    ancs_notification_cb_t on_notification;
    ancs_state_cb_t on_state;
    void *ctx;
    /* Optional terminal result for an accepted forget-bond operation. */
    ancs_bond_reset_cb_t on_bond_reset_result;
} ancs_client_config_t;

/*
 * Callbacks run synchronously on the NimBLE host task. The notification pointer
 * remains valid only for the duration of the callback and must be copied before
 * it is retained or passed to another task. A successful forget-bond return only
 * accepts the operation; on_bond_reset_result reports its terminal outcome once.
 */
esp_err_t ancs_client_start(const ancs_client_config_t *config);
esp_err_t ancs_client_forget_bond(void);

#ifdef __cplusplus
}
#endif
