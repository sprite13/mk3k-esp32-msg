#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BOARD_DEBOUNCE_US 30000U
#define BOARD_BEEP_MAX_DURATION_MS 60000U

typedef struct {
    bool stable_down;
    bool candidate_down;
    uint64_t candidate_since_us;
} board_debounce_state_t;

void board_debounce_init(board_debounce_state_t *state, bool down, uint64_t now_us);
bool board_debounce_update(board_debounce_state_t *state, bool down, uint64_t now_us);
uint32_t board_bound_beep_duration_ms(uint32_t duration_ms);
