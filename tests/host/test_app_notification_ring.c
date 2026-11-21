#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "app_notification_ring.h"
#include "notification_normalizer.h"
#include "test_check.h"

static notification_t notification(uint32_t uid, const char *app_id)
{
    notification_t item = {.uid = uid};
    strncpy(item.app_id, app_id, sizeof(item.app_id) - 1U);
    return item;
}

static void check_pop(app_notification_ring_t *ring, uint32_t uid)
{
    notification_t item;
    TEST_CHECK(app_notification_ring_pop(ring, &item));
    TEST_CHECK(item.uid == uid);
}

static void test_unrelated_apps_never_consume_capacity(void)
{
    app_notification_ring_t ring;
    TEST_CHECK(app_notification_ring_init(&ring, 2U));

    const notification_t other = notification(1U, "com.example.Other");
    TEST_CHECK(app_notification_ring_push(&ring, &other) ==
               APP_NOTIFICATION_RING_REJECTED);
    TEST_CHECK(app_notification_ring_size(&ring) == 0U);
}

static void test_full_ring_evicts_exactly_oldest(void)
{
    app_notification_ring_t ring;
    TEST_CHECK(app_notification_ring_init(&ring, 3U));
    for (uint32_t uid = 1U; uid <= 3U; ++uid) {
        const notification_t item = notification(uid, TELEGRAM_IOS_BUNDLE_ID);
        TEST_CHECK(app_notification_ring_push(&ring, &item) ==
                   APP_NOTIFICATION_RING_ENQUEUED);
    }
    const notification_t newest = notification(4U, TELEGRAM_IOS_BUNDLE_ID);
    TEST_CHECK(app_notification_ring_push(&ring, &newest) ==
               APP_NOTIFICATION_RING_REPLACED_OLDEST);
    TEST_CHECK(app_notification_ring_size(&ring) == 3U);
    check_pop(&ring, 2U);
    check_pop(&ring, 3U);
    check_pop(&ring, 4U);
}

static void test_full_pop_push_interleaving_does_not_over_evict(void)
{
    app_notification_ring_t ring;
    TEST_CHECK(app_notification_ring_init(&ring, 3U));
    for (uint32_t uid = 1U; uid <= 3U; ++uid) {
        const notification_t item = notification(uid, TELEGRAM_IOS_BUNDLE_ID);
        TEST_CHECK(app_notification_ring_push(&ring, &item) ==
                   APP_NOTIFICATION_RING_ENQUEUED);
    }

    check_pop(&ring, 1U);
    const notification_t newest = notification(4U, TELEGRAM_IOS_BUNDLE_ID);
    TEST_CHECK(app_notification_ring_push(&ring, &newest) ==
               APP_NOTIFICATION_RING_ENQUEUED);
    TEST_CHECK(app_notification_ring_size(&ring) == 3U);
    check_pop(&ring, 2U);
    check_pop(&ring, 3U);
    check_pop(&ring, 4U);
}

static void test_burst_preserves_newest_capacity(void)
{
    app_notification_ring_t ring;
    TEST_CHECK(app_notification_ring_init(&ring, 3U));
    for (uint32_t uid = 1U; uid <= 8U; ++uid) {
        const notification_t item = notification(uid, TELEGRAM_IOS_BUNDLE_ID);
        (void)app_notification_ring_push(&ring, &item);
        TEST_CHECK(app_notification_ring_size(&ring) <= 3U);
    }
    check_pop(&ring, 6U);
    check_pop(&ring, 7U);
    check_pop(&ring, 8U);
}

int main(void)
{
    test_unrelated_apps_never_consume_capacity();
    test_full_ring_evicts_exactly_oldest();
    test_full_pop_push_interleaving_does_not_over_evict();
    test_burst_preserves_newest_capacity();
    return 0;
}
