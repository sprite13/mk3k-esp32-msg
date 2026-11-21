#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ancs_request_queue.h"

#define ANCS_CONN_HANDLE_NONE UINT16_MAX

typedef struct {
    uint32_t uid;
    uint32_t generation;
    uint16_t conn_handle;
} ancs_write_token_t;

typedef enum {
    ANCS_WRITE_STALE,
    ANCS_WRITE_START_TIMEOUT,
    ANCS_WRITE_ADVANCE,
    ANCS_WRITE_ERROR,
} ancs_write_result_t;

typedef struct {
    ancs_request_queue_t queue;
    uint32_t active_uid;
    uint32_t generation;
    uint16_t conn_handle;
    uint8_t reconnect_failures;
    bool active;
    bool write_in_flight;
    bool active_canceled;
    bool response_waiting;
    bool recovery_required;
} ancs_transport_t;

void ancs_transport_init(ancs_transport_t *transport);
void ancs_transport_connected(ancs_transport_t *transport, uint16_t conn_handle);
void ancs_transport_disconnected(ancs_transport_t *transport);
bool ancs_transport_enqueue(ancs_transport_t *transport, uint32_t uid);
bool ancs_transport_begin_write(ancs_transport_t *transport,
                                ancs_write_token_t *token);
bool ancs_transport_cancel_uid(ancs_transport_t *transport, uint32_t uid);
bool ancs_transport_cancel_active(ancs_transport_t *transport);
ancs_write_result_t ancs_transport_write_complete(ancs_transport_t *transport,
                                                  uint16_t conn_handle,
                                                  uint32_t generation,
                                                  bool success);
bool ancs_transport_finish_response(ancs_transport_t *transport);
bool ancs_transport_write_in_flight(const ancs_transport_t *transport);
bool ancs_transport_response_waiting(const ancs_transport_t *transport);
bool ancs_transport_recovery_required(const ancs_transport_t *transport);
bool ancs_transport_has_active(const ancs_transport_t *transport);
uint32_t ancs_transport_active_uid(const ancs_transport_t *transport);
uint32_t ancs_transport_take_reconnect_delay(ancs_transport_t *transport);
void ancs_transport_subscriptions_ready(ancs_transport_t *transport);

typedef struct {
    uint16_t start_handle;
    uint16_t end_handle;
} ancs_handle_range_t;

typedef enum {
    ANCS_SERVICE_CHANGED_REDISCOVER,
    ANCS_SERVICE_CHANGED_RECOVER,
} ancs_service_changed_decision_t;

ancs_service_changed_decision_t
ancs_service_changed_decide(const uint8_t *data, size_t length,
                            ancs_handle_range_t *range);

typedef enum {
    ANCS_BOND_RESET_IDLE,
    ANCS_BOND_RESET_WAIT_LINK_STOP,
    ANCS_BOND_RESET_WAIT_CLEAR,
    ANCS_BOND_RESET_WAIT_RESTART,
} ancs_bond_reset_state_t;

typedef struct {
    ancs_bond_reset_state_t state;
} ancs_bond_reset_t;

typedef enum {
    ANCS_BOND_ACTION_NONE,
    ANCS_BOND_ACTION_DISCONNECT,
    ANCS_BOND_ACTION_STOP_ADVERTISING,
    ANCS_BOND_ACTION_CLEAR_BONDS,
    ANCS_BOND_ACTION_QUEUE_RESTART,
    ANCS_BOND_ACTION_ADVERTISE,
} ancs_bond_action_t;

typedef enum {
    ANCS_BOND_RESULT_NONE,
    ANCS_BOND_RESULT_SUCCEEDED,
    ANCS_BOND_RESULT_FAILED,
} ancs_bond_result_t;

void ancs_bond_reset_init(ancs_bond_reset_t *reset);
ancs_bond_action_t ancs_bond_reset_request(ancs_bond_reset_t *reset,
                                           bool connected, bool advertising);
ancs_bond_action_t ancs_bond_reset_link_stopped(ancs_bond_reset_t *reset);
ancs_bond_action_t ancs_bond_reset_cleared(ancs_bond_reset_t *reset,
                                           bool success);
ancs_bond_action_t ancs_bond_reset_restart(ancs_bond_reset_t *reset);
ancs_bond_result_t ancs_bond_reset_finish(ancs_bond_reset_t *reset,
                                          bool success);
