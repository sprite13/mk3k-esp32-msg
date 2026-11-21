#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ANCS_REQUEST_QUEUE_CAPACITY 8U

typedef struct {
    uint32_t uids[ANCS_REQUEST_QUEUE_CAPACITY];
    uint8_t head;
    uint8_t count;
} ancs_request_queue_t;

void ancs_request_queue_init(ancs_request_queue_t *queue);
bool ancs_request_queue_push(ancs_request_queue_t *queue, uint32_t uid);
bool ancs_request_queue_pop(ancs_request_queue_t *queue, uint32_t *uid);
size_t ancs_request_queue_remove(ancs_request_queue_t *queue, uint32_t uid);
