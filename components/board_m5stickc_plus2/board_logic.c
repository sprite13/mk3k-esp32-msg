#include "board_logic.h"

void board_debounce_init(board_debounce_state_t *state, bool down, uint64_t now_us)
{
    state->stable_down = down;
    state->candidate_down = down;
    state->candidate_since_us = now_us;
}

bool board_debounce_update(board_debounce_state_t *state, bool down, uint64_t now_us)
{
    if (down == state->stable_down) {
        state->candidate_down = down;
        state->candidate_since_us = now_us;
        return state->stable_down;
    }

    if (down != state->candidate_down || now_us < state->candidate_since_us) {
        state->candidate_down = down;
        state->candidate_since_us = now_us;
        return state->stable_down;
    }

    if ((now_us - state->candidate_since_us) >= BOARD_DEBOUNCE_US) {
        state->stable_down = down;
    }

    return state->stable_down;
}

uint32_t board_bound_beep_duration_ms(uint32_t duration_ms)
{
    if (duration_ms > BOARD_BEEP_MAX_DURATION_MS) {
        return BOARD_BEEP_MAX_DURATION_MS;
    }
    return duration_ms;
}
