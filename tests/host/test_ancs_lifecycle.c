#include <stdint.h>

#include "ancs_lifecycle.h"
#include "test_check.h"

static void test_removed_while_write_pending_waits_for_callback(void)
{
    ancs_transport_t transport;
    ancs_write_token_t first;
    ancs_write_token_t second;

    ancs_transport_init(&transport);
    ancs_transport_connected(&transport, 7U);
    TEST_CHECK(ancs_transport_enqueue(&transport, 10U));
    TEST_CHECK(ancs_transport_enqueue(&transport, 11U));
    TEST_CHECK(ancs_transport_begin_write(&transport, &first));
    TEST_CHECK(!ancs_transport_cancel_uid(&transport, 10U));
    TEST_CHECK(!ancs_transport_begin_write(&transport, &second));
    TEST_CHECK(ancs_transport_write_complete(&transport, first.conn_handle,
                                             first.generation, true) ==
               ANCS_WRITE_ADVANCE);
    TEST_CHECK(ancs_transport_begin_write(&transport, &second));
    TEST_CHECK(second.uid == 11U);
}

static void test_timeout_while_write_pending_waits_for_callback(void)
{
    ancs_transport_t transport;
    ancs_write_token_t token;

    ancs_transport_init(&transport);
    ancs_transport_connected(&transport, 4U);
    TEST_CHECK(ancs_transport_enqueue(&transport, 20U));
    TEST_CHECK(ancs_transport_begin_write(&transport, &token));
    TEST_CHECK(!ancs_transport_cancel_active(&transport));
    TEST_CHECK(ancs_transport_write_complete(&transport, token.conn_handle,
                                             token.generation, true) ==
               ANCS_WRITE_ADVANCE);
    TEST_CHECK(!ancs_transport_has_active(&transport));
}

static void test_stale_callback_cannot_release_current_generation(void)
{
    ancs_transport_t transport;
    ancs_write_token_t old;
    ancs_write_token_t current;

    ancs_transport_init(&transport);
    ancs_transport_connected(&transport, 4U);
    TEST_CHECK(ancs_transport_enqueue(&transport, 20U));
    TEST_CHECK(ancs_transport_begin_write(&transport, &old));
    ancs_transport_disconnected(&transport);

    ancs_transport_connected(&transport, 4U);
    TEST_CHECK(ancs_transport_enqueue(&transport, 21U));
    TEST_CHECK(ancs_transport_begin_write(&transport, &current));
    TEST_CHECK(ancs_transport_write_complete(&transport, old.conn_handle,
                                             old.generation, true) ==
               ANCS_WRITE_STALE);
    TEST_CHECK(ancs_transport_write_in_flight(&transport));
    TEST_CHECK(ancs_transport_active_uid(&transport) == 21U);
    TEST_CHECK(ancs_transport_write_complete(&transport, current.conn_handle,
                                             current.generation, true) ==
               ANCS_WRITE_START_TIMEOUT);
    TEST_CHECK(ancs_transport_response_waiting(&transport));
}

static void test_queue_overflow_requires_recovery(void)
{
    ancs_transport_t transport;

    ancs_transport_init(&transport);
    ancs_transport_connected(&transport, 1U);
    for (uint32_t uid = 0U; uid < ANCS_REQUEST_QUEUE_CAPACITY; ++uid) {
        TEST_CHECK(ancs_transport_enqueue(&transport, uid));
    }
    TEST_CHECK(!ancs_transport_enqueue(&transport, 99U));
    TEST_CHECK(ancs_transport_recovery_required(&transport));
}

static void test_backoff_resets_only_when_subscriptions_are_ready(void)
{
    ancs_transport_t transport;
    static const uint32_t expected[] = {
        1000U, 2000U, 5000U, 10000U, 30000U,
        60000U, 60000U, 60000U, 60000U,
    };

    ancs_transport_init(&transport);
    for (size_t index = 0U;
         index < sizeof(expected) / sizeof(expected[0]); ++index) {
        TEST_CHECK(ancs_transport_take_reconnect_delay(&transport) ==
                   expected[index]);
        if (index == 0U) {
            ancs_transport_connected(&transport, 2U);
        }
    }
    ancs_transport_subscriptions_ready(&transport);
    TEST_CHECK(ancs_transport_take_reconnect_delay(&transport) == 1000U);
}

static void test_service_changed_validation_and_decision(void)
{
    ancs_handle_range_t range;
    const uint8_t valid[] = {0x01U, 0x00U, 0xffU, 0xffU};
    const uint8_t reversed[] = {0x20U, 0x00U, 0x10U, 0x00U};
    const uint8_t short_value[] = {0x01U, 0x00U, 0xffU};

    TEST_CHECK(ancs_service_changed_decide(valid, sizeof(valid), &range) ==
               ANCS_SERVICE_CHANGED_REDISCOVER);
    TEST_CHECK(range.start_handle == 1U);
    TEST_CHECK(range.end_handle == UINT16_MAX);
    TEST_CHECK(ancs_service_changed_decide(reversed, sizeof(reversed), &range) ==
               ANCS_SERVICE_CHANGED_RECOVER);
    TEST_CHECK(ancs_service_changed_decide(short_value, sizeof(short_value),
                                           &range) ==
               ANCS_SERVICE_CHANGED_RECOVER);
}

static void test_bond_reset_queues_restart_after_link_stop(void)
{
    ancs_bond_reset_t reset;

    ancs_bond_reset_init(&reset);
    TEST_CHECK(ancs_bond_reset_request(&reset, true, false) ==
               ANCS_BOND_ACTION_DISCONNECT);
    TEST_CHECK(ancs_bond_reset_link_stopped(&reset) ==
               ANCS_BOND_ACTION_CLEAR_BONDS);
    TEST_CHECK(ancs_bond_reset_cleared(&reset, true) ==
               ANCS_BOND_ACTION_QUEUE_RESTART);
    TEST_CHECK(ancs_bond_reset_restart(&reset) ==
               ANCS_BOND_ACTION_ADVERTISE);
    TEST_CHECK(ancs_bond_reset_finish(&reset, true) ==
               ANCS_BOND_RESULT_SUCCEEDED);
    TEST_CHECK(ancs_bond_reset_finish(&reset, false) ==
               ANCS_BOND_RESULT_NONE);

    ancs_bond_reset_init(&reset);
    TEST_CHECK(ancs_bond_reset_request(&reset, false, true) ==
               ANCS_BOND_ACTION_STOP_ADVERTISING);
    TEST_CHECK(ancs_bond_reset_link_stopped(&reset) ==
               ANCS_BOND_ACTION_CLEAR_BONDS);
}

static void test_bond_reset_late_failure_reports_once(void)
{
    ancs_bond_reset_t reset;

    ancs_bond_reset_init(&reset);
    TEST_CHECK(ancs_bond_reset_request(&reset, true, false) ==
               ANCS_BOND_ACTION_DISCONNECT);
    TEST_CHECK(ancs_bond_reset_finish(&reset, false) ==
               ANCS_BOND_RESULT_FAILED);
    TEST_CHECK(ancs_bond_reset_finish(&reset, false) ==
               ANCS_BOND_RESULT_NONE);

    ancs_bond_reset_init(&reset);
    TEST_CHECK(ancs_bond_reset_request(&reset, false, false) ==
               ANCS_BOND_ACTION_CLEAR_BONDS);
    TEST_CHECK(ancs_bond_reset_cleared(&reset, true) ==
               ANCS_BOND_ACTION_QUEUE_RESTART);
    TEST_CHECK(ancs_bond_reset_restart(&reset) ==
               ANCS_BOND_ACTION_ADVERTISE);
    TEST_CHECK(ancs_bond_reset_finish(&reset, false) ==
               ANCS_BOND_RESULT_FAILED);
    TEST_CHECK(ancs_bond_reset_finish(&reset, false) ==
               ANCS_BOND_RESULT_NONE);
}

static void test_bond_reset_stale_transitions_do_not_report(void)
{
    ancs_bond_reset_t reset;

    ancs_bond_reset_init(&reset);
    TEST_CHECK(ancs_bond_reset_finish(&reset, true) ==
               ANCS_BOND_RESULT_NONE);
    TEST_CHECK(ancs_bond_reset_link_stopped(&reset) ==
               ANCS_BOND_ACTION_NONE);
    TEST_CHECK(ancs_bond_reset_restart(&reset) ==
               ANCS_BOND_ACTION_NONE);
}

int main(void)
{
    test_removed_while_write_pending_waits_for_callback();
    test_timeout_while_write_pending_waits_for_callback();
    test_stale_callback_cannot_release_current_generation();
    test_queue_overflow_requires_recovery();
    test_backoff_resets_only_when_subscriptions_are_ready();
    test_service_changed_validation_and_decision();
    test_bond_reset_queues_restart_after_link_stop();
    test_bond_reset_late_failure_reports_once();
    test_bond_reset_stale_transitions_do_not_report();
    return 0;
}
