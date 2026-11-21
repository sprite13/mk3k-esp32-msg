#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "app_notification_admission.h"
#include "notification_normalizer.h"
#include "test_check.h"

static notification_t notification(uint32_t uid, const char *app_id)
{
    notification_t item = {.uid = uid};
    strncpy(item.app_id, app_id, sizeof(item.app_id) - 1U);
    return item;
}

static void apply_policy(notification_t *items, size_t *count, size_t capacity,
                         const notification_t *item)
{
    const app_notification_admission_t action =
        app_notification_admission_decide(item, *count, capacity);
    if (action == APP_NOTIFICATION_REJECT) {
        return;
    }
    if (action == APP_NOTIFICATION_REPLACE_OLDEST) {
        memmove(&items[0], &items[1], (capacity - 1U) * sizeof(items[0]));
        *count = capacity - 1U;
    }
    items[*count] = *item;
    ++(*count);
}

static void test_unrelated_apps_never_consume_capacity(void)
{
    notification_t items[2] = {0};
    size_t count = 0U;
    notification_t item = notification(1U, "com.example.Other");
    apply_policy(items, &count, 2U, &item);
    TEST_CHECK(count == 0U);

    item = notification(2U, TELEGRAM_IOS_BUNDLE_ID);
    apply_policy(items, &count, 2U, &item);
    TEST_CHECK(count == 1U);
    TEST_CHECK(items[0].uid == 2U);
}

static void test_full_queue_preserves_newest_telegram_notifications(void)
{
    notification_t items[3] = {0};
    size_t count = 0U;
    for (uint32_t uid = 1U; uid <= 5U; ++uid) {
        notification_t item = notification(uid, TELEGRAM_IOS_BUNDLE_ID);
        apply_policy(items, &count, 3U, &item);
    }

    TEST_CHECK(count == 3U);
    TEST_CHECK(items[0].uid == 3U);
    TEST_CHECK(items[1].uid == 4U);
    TEST_CHECK(items[2].uid == 5U);
}

static void test_zero_capacity_rejects_safely(void)
{
    const notification_t item = notification(1U, TELEGRAM_IOS_BUNDLE_ID);
    TEST_CHECK(app_notification_admission_decide(&item, 0U, 0U) ==
               APP_NOTIFICATION_REJECT);
}

int main(void)
{
    test_unrelated_apps_never_consume_capacity();
    test_full_queue_preserves_newest_telegram_notifications();
    test_zero_capacity_rejects_safely();
    return 0;
}
