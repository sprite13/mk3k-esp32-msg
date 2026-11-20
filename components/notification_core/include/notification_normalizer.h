#pragma once

#include <stdbool.h>

#include "notification.h"

#define TELEGRAM_IOS_BUNDLE_ID "ph.telegra.Telegraph"

bool notification_is_telegram(const notification_t *item);
void notification_normalize(notification_t *item);
