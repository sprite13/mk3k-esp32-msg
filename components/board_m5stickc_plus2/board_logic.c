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

void board_buzzer_start(board_buzzer_state_t *state, uint64_t now_us,
                        uint32_t duration_ms)
{
    const uint64_t duration_us =
        (uint64_t)board_bound_beep_duration_ms(duration_ms) * 1000U;
    state->active = duration_us > 0U;
    state->deadline_us = now_us + duration_us;
}

bool board_buzzer_expire(board_buzzer_state_t *state, uint64_t now_us)
{
    if (!state->active || now_us < state->deadline_us) {
        return false;
    }

    state->active = false;
    return true;
}

uint64_t board_buzzer_remaining_us(const board_buzzer_state_t *state, uint64_t now_us)
{
    if (!state->active || now_us >= state->deadline_us) {
        return 0U;
    }
    return state->deadline_us - now_us;
}
