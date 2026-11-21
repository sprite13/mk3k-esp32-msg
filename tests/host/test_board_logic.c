#include <stdbool.h>
#include <stdint.h>

#include "board_logic.h"
#include "test_check.h"

static void test_debounce_requires_30_ms_stable_sample(void)
{
    board_debounce_state_t state;

    board_debounce_init(&state, false, 1000U);
    TEST_CHECK(!board_debounce_update(&state, true, 1000U));
    TEST_CHECK(!board_debounce_update(&state, true, 30999U));
    TEST_CHECK(board_debounce_update(&state, true, 31000U));
}

static void test_debounce_restarts_after_bounce(void)
{
    board_debounce_state_t state;

    board_debounce_init(&state, false, 0U);
    TEST_CHECK(!board_debounce_update(&state, true, 1000U));
    TEST_CHECK(!board_debounce_update(&state, false, 20000U));
    TEST_CHECK(!board_debounce_update(&state, true, 25000U));
    TEST_CHECK(!board_debounce_update(&state, true, 54999U));
    TEST_CHECK(board_debounce_update(&state, true, 55000U));
}

static void test_debounce_recovers_from_non_monotonic_time(void)
{
    board_debounce_state_t state;

    board_debounce_init(&state, false, 100000U);
    TEST_CHECK(!board_debounce_update(&state, true, 100000U));
    TEST_CHECK(!board_debounce_update(&state, true, 90000U));
    TEST_CHECK(!board_debounce_update(&state, true, 119999U));
    TEST_CHECK(board_debounce_update(&state, true, 120000U));
}

static void test_beep_duration_is_bounded(void)
{
    TEST_CHECK(board_bound_beep_duration_ms(0U) == 0U);
    TEST_CHECK(board_bound_beep_duration_ms(80U) == 80U);
    TEST_CHECK(board_bound_beep_duration_ms(BOARD_BEEP_MAX_DURATION_MS) ==
               BOARD_BEEP_MAX_DURATION_MS);
    TEST_CHECK(board_bound_beep_duration_ms(UINT32_MAX) ==
               BOARD_BEEP_MAX_DURATION_MS);
}

static void test_replacement_uses_new_deadline(void)
{
    board_buzzer_state_t state = {0};

    board_buzzer_start(&state, 0U, 80U);
    board_buzzer_start(&state, 20000U, 100U);

    TEST_CHECK(board_buzzer_remaining_us(&state, 80000U) == 40000U);
    TEST_CHECK(!board_buzzer_expire(&state, 119999U));
    TEST_CHECK(board_buzzer_expire(&state, 120000U));
}

static void test_stale_timeout_cannot_stop_replacement(void)
{
    board_buzzer_state_t state = {0};

    board_buzzer_start(&state, 0U, 80U);
    const uint64_t stale_timeout_us = 80000U;
    board_buzzer_start(&state, 40000U, 80U);

    TEST_CHECK(!board_buzzer_expire(&state, stale_timeout_us));
    TEST_CHECK(state.active);
    TEST_CHECK(board_buzzer_expire(&state, 120000U));
    TEST_CHECK(!state.active);
}

static void test_elapsed_one_ms_beep_expires_before_wait(void)
{
    board_buzzer_state_t state = {0};
    uint64_t remaining_us = UINT64_MAX;

    board_buzzer_start(&state, 5000U, 1U);

    TEST_CHECK(board_buzzer_prepare_wait(&state, 6001U, &remaining_us));
    TEST_CHECK(!state.active);
    TEST_CHECK(remaining_us == 0U);
}

int main(void)
{
    test_debounce_requires_30_ms_stable_sample();
    test_debounce_restarts_after_bounce();
    test_debounce_recovers_from_non_monotonic_time();
    test_beep_duration_is_bounded();
    test_replacement_uses_new_deadline();
    test_stale_timeout_cannot_stop_replacement();
    test_elapsed_one_ms_beep_expires_before_wait();
    return 0;
}
