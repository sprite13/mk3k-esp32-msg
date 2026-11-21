#pragma once

#include <stdbool.h>

typedef enum {
    DISPLAY_LIFECYCLE_UNINITIALIZED,
    DISPLAY_LIFECYCLE_AWAKE,
    DISPLAY_LIFECYCLE_SLEEPING,
    DISPLAY_LIFECYCLE_FAILED,
} display_lifecycle_state_t;

typedef struct {
    display_lifecycle_state_t state;
} display_lifecycle_t;

typedef enum {
    DISPLAY_SLEEP_COMPLETED,
    DISPLAY_SLEEP_RECOVERED,
    DISPLAY_SLEEP_FAILED,
} display_sleep_result_t;

void display_lifecycle_init(display_lifecycle_t *lifecycle);
bool display_lifecycle_can_render(const display_lifecycle_t *lifecycle);
bool display_lifecycle_can_sleep(const display_lifecycle_t *lifecycle);
bool display_lifecycle_failed(const display_lifecycle_t *lifecycle);
void display_lifecycle_mark_awake(display_lifecycle_t *lifecycle);
void display_lifecycle_mark_transfer_timeout(display_lifecycle_t *lifecycle);
void display_lifecycle_resolve_sleep(display_lifecycle_t *lifecycle,
                                     display_sleep_result_t result);
