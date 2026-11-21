#include "ancs_lifecycle.h"

#include <string.h>

#include "power_manager.h"

static void clear_active(ancs_transport_t *transport)
{
    transport->active_uid = 0U;
    transport->active = false;
    transport->active_canceled = false;
    transport->response_waiting = false;
}

void ancs_transport_init(ancs_transport_t *transport)
{
    memset(transport, 0, sizeof(*transport));
    ancs_request_queue_init(&transport->queue);
    transport->conn_handle = ANCS_CONN_HANDLE_NONE;
}

void ancs_transport_connected(ancs_transport_t *transport, uint16_t conn_handle)
{
    transport->conn_handle = conn_handle;
}

void ancs_transport_disconnected(ancs_transport_t *transport)
{
    ancs_request_queue_init(&transport->queue);
    clear_active(transport);
    transport->write_in_flight = false;
    transport->recovery_required = false;
    transport->conn_handle = ANCS_CONN_HANDLE_NONE;
    transport->generation++;
}

bool ancs_transport_enqueue(ancs_transport_t *transport, uint32_t uid)
{
    if (!ancs_request_queue_push(&transport->queue, uid)) {
        transport->recovery_required = true;
        return false;
    }
    return true;
}

bool ancs_transport_begin_write(ancs_transport_t *transport,
                                ancs_write_token_t *token)
{
    uint32_t uid;

    if (transport->conn_handle == ANCS_CONN_HANDLE_NONE || transport->active ||
        transport->write_in_flight || transport->recovery_required ||
        token == NULL || !ancs_request_queue_pop(&transport->queue, &uid)) {
        return false;
    }

    transport->active = true;
    transport->active_uid = uid;
    transport->write_in_flight = true;
    transport->active_canceled = false;
    transport->response_waiting = false;
    transport->generation++;
    token->uid = uid;
    token->generation = transport->generation;
    token->conn_handle = transport->conn_handle;
    return true;
}

bool ancs_transport_cancel_uid(ancs_transport_t *transport, uint32_t uid)
{
    (void)ancs_request_queue_remove(&transport->queue, uid);
    if (!transport->active || transport->active_uid != uid) {
        return false;
    }
    return ancs_transport_cancel_active(transport);
}

bool ancs_transport_cancel_active(ancs_transport_t *transport)
{
    if (!transport->active) {
        return false;
    }
    if (transport->write_in_flight) {
        transport->active_canceled = true;
        return false;
    }
    clear_active(transport);
    return true;
}

ancs_write_result_t ancs_transport_write_complete(ancs_transport_t *transport,
                                                  uint16_t conn_handle,
                                                  uint32_t generation,
                                                  bool success)
{
    if (!transport->write_in_flight || conn_handle != transport->conn_handle ||
        generation != transport->generation) {
        return ANCS_WRITE_STALE;
    }

    transport->write_in_flight = false;
    if (transport->active_canceled) {
        clear_active(transport);
        return ANCS_WRITE_ADVANCE;
    }
    if (!success) {
        clear_active(transport);
        return ANCS_WRITE_ERROR;
    }
    transport->response_waiting = true;
    return ANCS_WRITE_START_TIMEOUT;
}

bool ancs_transport_finish_response(ancs_transport_t *transport)
{
    if (!transport->active || transport->write_in_flight) {
        return false;
    }
    clear_active(transport);
    return true;
}

bool ancs_transport_write_in_flight(const ancs_transport_t *transport)
{
    return transport->write_in_flight;
}

bool ancs_transport_response_waiting(const ancs_transport_t *transport)
{
    return transport->response_waiting;
}

bool ancs_transport_recovery_required(const ancs_transport_t *transport)
{
    return transport->recovery_required;
}

bool ancs_transport_has_active(const ancs_transport_t *transport)
{
    return transport->active;
}

uint32_t ancs_transport_active_uid(const ancs_transport_t *transport)
{
    return transport->active ? transport->active_uid : 0U;
}

uint32_t ancs_transport_take_reconnect_delay(ancs_transport_t *transport)
{
    const uint32_t delay =
        power_reconnect_delay_ms(transport->reconnect_failures);
    if (transport->reconnect_failures < UINT8_MAX) {
        transport->reconnect_failures++;
    }
    return delay;
}

void ancs_transport_subscriptions_ready(ancs_transport_t *transport)
{
    transport->reconnect_failures = 0U;
}

ancs_peer_admission_t
ancs_peer_admission_decide(const ancs_peer_identity_t *bonded_peers,
                           size_t bonded_peer_count,
                           const ancs_peer_identity_t *peer)
{
    if (bonded_peer_count == 0U) {
        return ANCS_PEER_ADMIT_OPEN_PAIRING;
    }
    if (bonded_peers == NULL || peer == NULL) {
        return ANCS_PEER_REJECT_UNKNOWN;
    }
    for (size_t index = 0U; index < bonded_peer_count; ++index) {
        if (bonded_peers[index].type == peer->type &&
            memcmp(bonded_peers[index].value, peer->value,
                   sizeof(peer->value)) == 0) {
            return ANCS_PEER_ADMIT_BONDED;
        }
    }
    return ANCS_PEER_REJECT_UNKNOWN;
}

int ancs_unpair_peers(const ancs_peer_identity_t *bonded_peers,
                      size_t bonded_peer_count,
                      ancs_unpair_peer_fn unpair_peer, void *context)
{
    int first_error = 0;

    if (bonded_peer_count == 0U) {
        return 0;
    }
    if (bonded_peers == NULL || unpair_peer == NULL) {
        return -1;
    }
    for (size_t index = 0U; index < bonded_peer_count; ++index) {
        const int rc = unpair_peer(&bonded_peers[index], context);
        if (first_error == 0 && rc != 0) {
            first_error = rc;
        }
    }
    return first_error;
}

ancs_service_changed_decision_t
ancs_service_changed_decide(const uint8_t *data, size_t length,
                            ancs_handle_range_t *range)
{
    if (data == NULL || range == NULL || length != 4U) {
        return ANCS_SERVICE_CHANGED_RECOVER;
    }
    range->start_handle = (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
    range->end_handle = (uint16_t)data[2] | ((uint16_t)data[3] << 8U);
    if (range->start_handle == 0U || range->start_handle > range->end_handle) {
        return ANCS_SERVICE_CHANGED_RECOVER;
    }
    return ANCS_SERVICE_CHANGED_REDISCOVER;
}

void ancs_bond_reset_init(ancs_bond_reset_t *reset)
{
    reset->state = ANCS_BOND_RESET_IDLE;
}

ancs_bond_action_t ancs_bond_reset_request(ancs_bond_reset_t *reset,
                                           bool connected, bool advertising)
{
    if (reset->state != ANCS_BOND_RESET_IDLE) {
        return ANCS_BOND_ACTION_NONE;
    }
    if (connected || advertising) {
        reset->state = ANCS_BOND_RESET_WAIT_LINK_STOP;
        return connected ? ANCS_BOND_ACTION_DISCONNECT
                         : ANCS_BOND_ACTION_STOP_ADVERTISING;
    }
    reset->state = ANCS_BOND_RESET_WAIT_CLEAR;
    return ANCS_BOND_ACTION_CLEAR_BONDS;
}

ancs_bond_action_t ancs_bond_reset_link_stopped(ancs_bond_reset_t *reset)
{
    if (reset->state != ANCS_BOND_RESET_WAIT_LINK_STOP) {
        return ANCS_BOND_ACTION_NONE;
    }
    reset->state = ANCS_BOND_RESET_WAIT_CLEAR;
    return ANCS_BOND_ACTION_CLEAR_BONDS;
}

ancs_bond_action_t ancs_bond_reset_cleared(ancs_bond_reset_t *reset,
                                           bool success)
{
    if (reset->state != ANCS_BOND_RESET_WAIT_CLEAR) {
        return ANCS_BOND_ACTION_NONE;
    }
    if (!success) {
        return ANCS_BOND_ACTION_NONE;
    }
    reset->state = ANCS_BOND_RESET_WAIT_RESTART;
    return ANCS_BOND_ACTION_QUEUE_RESTART;
}

ancs_bond_action_t ancs_bond_reset_restart(ancs_bond_reset_t *reset)
{
    if (reset->state != ANCS_BOND_RESET_WAIT_RESTART) {
        return ANCS_BOND_ACTION_NONE;
    }
    return ANCS_BOND_ACTION_ADVERTISE;
}

ancs_bond_result_t ancs_bond_reset_finish(ancs_bond_reset_t *reset,
                                          bool success)
{
    if (reset == NULL || reset->state == ANCS_BOND_RESET_IDLE) {
        return ANCS_BOND_RESULT_NONE;
    }
    reset->state = ANCS_BOND_RESET_IDLE;
    return success ? ANCS_BOND_RESULT_SUCCEEDED : ANCS_BOND_RESULT_FAILED;
}
