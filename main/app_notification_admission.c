#include "app_notification_admission.h"

#include "notification_normalizer.h"

app_notification_admission_t app_notification_admission_decide(
    const notification_t *notification, size_t queued, size_t capacity)
{
    if (notification == NULL || capacity == 0U ||
        !notification_is_telegram(notification)) {
        return APP_NOTIFICATION_REJECT;
    }
    return queued < capacity ? APP_NOTIFICATION_ENQUEUE :
                               APP_NOTIFICATION_REPLACE_OLDEST;
}
