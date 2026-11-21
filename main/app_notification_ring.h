#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "notification.h"

#define APP_NOTIFICATION_RING_MAX_CAPACITY 8U

typedef enum {
    APP_NOTIFICATION_RING_REJECTED,
    APP_NOTIFICATION_RING_ENQUEUED,
    APP_NOTIFICATION_RING_REPLACED_OLDEST,
} app_notification_ring_result_t;

typedef struct {
    notification_t items[APP_NOTIFICATION_RING_MAX_CAPACITY];
    size_t head;
    size_t count;
    size_t capacity;
} app_notification_ring_t;

/* The caller provides synchronization; these operations are pure ring logic. */
bool app_notification_ring_init(app_notification_ring_t *ring,
                                size_t capacity);
app_notification_ring_result_t app_notification_ring_push(
    app_notification_ring_t *ring, const notification_t *notification);
bool app_notification_ring_pop(app_notification_ring_t *ring,
                               notification_t *notification);
size_t app_notification_ring_size(app_notification_ring_t *ring);
