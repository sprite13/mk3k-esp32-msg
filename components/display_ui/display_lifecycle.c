#include "display_lifecycle.h"

#include <stddef.h>

void display_lifecycle_init(display_lifecycle_t *lifecycle)
{
    if (lifecycle != NULL) {
        lifecycle->state = DISPLAY_LIFECYCLE_AWAKE;
    }
}

bool display_lifecycle_can_render(const display_lifecycle_t *lifecycle)
{
    return lifecycle != NULL &&
           (lifecycle->state == DISPLAY_LIFECYCLE_AWAKE ||
            lifecycle->state == DISPLAY_LIFECYCLE_SLEEPING);
}

bool display_lifecycle_can_sleep(const display_lifecycle_t *lifecycle)
{
    return lifecycle != NULL && lifecycle->state == DISPLAY_LIFECYCLE_AWAKE;
}

bool display_lifecycle_failed(const display_lifecycle_t *lifecycle)
{
    return lifecycle != NULL && lifecycle->state == DISPLAY_LIFECYCLE_FAILED;
}

void display_lifecycle_mark_awake(display_lifecycle_t *lifecycle)
{
    if (lifecycle != NULL && lifecycle->state != DISPLAY_LIFECYCLE_FAILED) {
        lifecycle->state = DISPLAY_LIFECYCLE_AWAKE;
    }
}

void display_lifecycle_mark_sleeping(display_lifecycle_t *lifecycle)
{
    if (lifecycle != NULL && lifecycle->state != DISPLAY_LIFECYCLE_FAILED) {
        lifecycle->state = DISPLAY_LIFECYCLE_SLEEPING;
    }
}

void display_lifecycle_mark_transfer_timeout(display_lifecycle_t *lifecycle)
{
    if (lifecycle != NULL) {
        lifecycle->state = DISPLAY_LIFECYCLE_FAILED;
    }
}
