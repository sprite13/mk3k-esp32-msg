#include "app_notification_ring.h"

#include "notification_normalizer.h"

static void ring_lock(app_notification_ring_t *ring)
{
    while (atomic_flag_test_and_set_explicit(&ring->lock,
                                              memory_order_acquire)) {
    }
}

static void ring_unlock(app_notification_ring_t *ring)
{
    atomic_flag_clear_explicit(&ring->lock, memory_order_release);
}

bool app_notification_ring_init(app_notification_ring_t *ring,
                                size_t capacity)
{
    if (ring == NULL || capacity == 0U ||
        capacity > APP_NOTIFICATION_RING_MAX_CAPACITY) {
        return false;
    }
    ring->head = 0U;
    ring->count = 0U;
    ring->capacity = capacity;
    atomic_flag_clear_explicit(&ring->lock, memory_order_relaxed);
    return true;
}

app_notification_ring_result_t app_notification_ring_push(
    app_notification_ring_t *ring, const notification_t *notification)
{
    if (ring == NULL || notification == NULL ||
        !notification_is_telegram(notification)) {
        return APP_NOTIFICATION_RING_REJECTED;
    }

    ring_lock(ring);
    if (ring->count == ring->capacity) {
        ring->items[ring->head] = *notification;
        ring->head = (ring->head + 1U) % ring->capacity;
        ring_unlock(ring);
        return APP_NOTIFICATION_RING_REPLACED_OLDEST;
    }

    const size_t tail = (ring->head + ring->count) % ring->capacity;
    ring->items[tail] = *notification;
    ++ring->count;
    ring_unlock(ring);
    return APP_NOTIFICATION_RING_ENQUEUED;
}

bool app_notification_ring_pop(app_notification_ring_t *ring,
                               notification_t *notification)
{
    if (ring == NULL || notification == NULL) {
        return false;
    }

    ring_lock(ring);
    if (ring->count == 0U) {
        ring_unlock(ring);
        return false;
    }
    *notification = ring->items[ring->head];
    ring->head = (ring->head + 1U) % ring->capacity;
    --ring->count;
    ring_unlock(ring);
    return true;
}

size_t app_notification_ring_size(app_notification_ring_t *ring)
{
    if (ring == NULL) {
        return 0U;
    }
    ring_lock(ring);
    const size_t count = ring->count;
    ring_unlock(ring);
    return count;
}
