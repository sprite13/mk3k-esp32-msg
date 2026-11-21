#pragma once

#include <stddef.h>

#include "notification.h"

typedef enum {
    APP_NOTIFICATION_REJECT,
    APP_NOTIFICATION_ENQUEUE,
    APP_NOTIFICATION_REPLACE_OLDEST,
} app_notification_admission_t;

app_notification_admission_t app_notification_admission_decide(
    const notification_t *notification, size_t queued, size_t capacity);
