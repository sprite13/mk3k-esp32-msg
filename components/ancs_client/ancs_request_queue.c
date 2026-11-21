#include "ancs_request_queue.h"

#include <string.h>

void ancs_request_queue_init(ancs_request_queue_t *queue)
{
    memset(queue, 0, sizeof(*queue));
}

bool ancs_request_queue_push(ancs_request_queue_t *queue, uint32_t uid)
{
    if (queue->count == ANCS_REQUEST_QUEUE_CAPACITY) {
        return false;
    }
    const uint8_t slot = (uint8_t)((queue->head + queue->count) %
                                   ANCS_REQUEST_QUEUE_CAPACITY);
    queue->uids[slot] = uid;
    queue->count++;
    return true;
}

bool ancs_request_queue_pop(ancs_request_queue_t *queue, uint32_t *uid)
{
    if (queue->count == 0U) {
        return false;
    }
    *uid = queue->uids[queue->head];
    queue->head = (uint8_t)((queue->head + 1U) % ANCS_REQUEST_QUEUE_CAPACITY);
    queue->count--;
    return true;
}

size_t ancs_request_queue_remove(ancs_request_queue_t *queue, uint32_t uid)
{
    uint32_t retained[ANCS_REQUEST_QUEUE_CAPACITY];
    uint8_t retained_count = 0U;
    size_t removed = 0U;
    uint32_t queued_uid = 0U;

    while (ancs_request_queue_pop(queue, &queued_uid)) {
        if (queued_uid == uid) {
            removed++;
        } else {
            retained[retained_count++] = queued_uid;
        }
    }
    for (uint8_t index = 0U; index < retained_count; ++index) {
        (void)ancs_request_queue_push(queue, retained[index]);
    }
    return removed;
}
