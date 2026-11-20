#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "notification.h"

#define NOTIFICATION_QUEUE_CAPACITY 5U

typedef struct {
    notification_t items[NOTIFICATION_QUEUE_CAPACITY];
    size_t count;
    size_t next_index;
    size_t selected_offset;
} notification_queue_t;

void notification_queue_init(notification_queue_t *queue);
void notification_queue_push(notification_queue_t *queue,
                             const notification_t *item);
size_t notification_queue_size(const notification_queue_t *queue);
const notification_t *notification_queue_current(
    const notification_queue_t *queue);
bool notification_queue_older(notification_queue_t *queue);
bool notification_queue_newer(notification_queue_t *queue);
