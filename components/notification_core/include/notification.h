#pragma once

#include <stdint.h>

#define NOTIFICATION_APP_ID_MAX 64
#define NOTIFICATION_TITLE_MAX 96
#define NOTIFICATION_SUBTITLE_MAX 96
#define NOTIFICATION_MESSAGE_MAX 256

typedef struct {
    uint32_t uid;
    char app_id[NOTIFICATION_APP_ID_MAX];
    char title[NOTIFICATION_TITLE_MAX];
    char subtitle[NOTIFICATION_SUBTITLE_MAX];
    char message[NOTIFICATION_MESSAGE_MAX];
} notification_t;
