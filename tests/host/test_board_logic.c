#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "board_logic.h"

static void test_debounce_requires_30_ms_stable_sample(void)
{
    board_debounce_state_t state;

    board_debounce_init(&state, false, 1000U);
    assert(!board_debounce_update(&state, true, 1000U));
    assert(!board_debounce_update(&state, true, 30999U));
    assert(board_debounce_update(&state, true, 31000U));
}

static void test_debounce_restarts_after_bounce(void)
{
    board_debounce_state_t state;

    board_debounce_init(&state, false, 0U);
    assert(!board_debounce_update(&state, true, 1000U));
    assert(!board_debounce_update(&state, false, 20000U));
    assert(!board_debounce_update(&state, true, 25000U));
    assert(!board_debounce_update(&state, true, 54999U));
    assert(board_debounce_update(&state, true, 55000U));
}

static void test_debounce_recovers_from_non_monotonic_time(void)
{
    board_debounce_state_t state;

    board_debounce_init(&state, false, 100000U);
    assert(!board_debounce_update(&state, true, 100000U));
    assert(!board_debounce_update(&state, true, 90000U));
    assert(!board_debounce_update(&state, true, 119999U));
    assert(board_debounce_update(&state, true, 120000U));
}

static void test_beep_duration_is_bounded(void)
{
    assert(board_bound_beep_duration_ms(0U) == 0U);
    assert(board_bound_beep_duration_ms(80U) == 80U);
    assert(board_bound_beep_duration_ms(BOARD_BEEP_MAX_DURATION_MS) ==
           BOARD_BEEP_MAX_DURATION_MS);
    assert(board_bound_beep_duration_ms(UINT32_MAX) == BOARD_BEEP_MAX_DURATION_MS);
}

int main(void)
{
    test_debounce_requires_30_ms_stable_sample();
    test_debounce_restarts_after_bounce();
    test_debounce_recovers_from_non_monotonic_time();
    test_beep_duration_is_bounded();
    return 0;
}
