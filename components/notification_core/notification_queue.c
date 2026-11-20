#include "notification_queue.h"

void notification_queue_init(notification_queue_t *queue)
{
    queue->count = 0U;
    queue->next_index = 0U;
    queue->selected_offset = 0U;
}

void notification_queue_push(notification_queue_t *queue,
                             const notification_t *item)
{
    queue->items[queue->next_index] = *item;
    queue->next_index = (queue->next_index + 1U) % NOTIFICATION_QUEUE_CAPACITY;
    if (queue->count < NOTIFICATION_QUEUE_CAPACITY) {
        ++queue->count;
    }
    queue->selected_offset = 0U;
}

size_t notification_queue_size(const notification_queue_t *queue)
{
    return queue->count;
}

const notification_t *notification_queue_current(
    const notification_queue_t *queue)
{
    if (queue->count == 0U) {
        return NULL;
    }

    const size_t newest_index =
        (queue->next_index + NOTIFICATION_QUEUE_CAPACITY - 1U) %
        NOTIFICATION_QUEUE_CAPACITY;
    const size_t selected_index =
        (newest_index + NOTIFICATION_QUEUE_CAPACITY - queue->selected_offset) %
        NOTIFICATION_QUEUE_CAPACITY;
    return &queue->items[selected_index];
}

bool notification_queue_older(notification_queue_t *queue)
{
    if (queue->selected_offset + 1U >= queue->count) {
        return false;
    }

    ++queue->selected_offset;
    return true;
}

bool notification_queue_newer(notification_queue_t *queue)
{
    if (queue->selected_offset == 0U) {
        return false;
    }

    --queue->selected_offset;
    return true;
}
