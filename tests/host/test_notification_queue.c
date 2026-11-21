#include <string.h>

#include "notification_queue.h"
#include "test_check.h"

static notification_t item(uint32_t uid, const char *title)
{
    notification_t notification = {.uid = uid};
    strncpy(notification.title, title, sizeof(notification.title) - 1U);
    return notification;
}

int main(void)
{
    notification_queue_t queue;
    notification_queue_init(&queue);
    TEST_CHECK(notification_queue_current(&queue) == NULL);

    for (uint32_t uid = 1; uid <= 6; ++uid) {
        notification_t notification = item(uid, "message");
        notification_queue_push(&queue, &notification);
        TEST_CHECK(notification_queue_current(&queue)->uid == uid);
    }

    TEST_CHECK(notification_queue_size(&queue) == 5U);
    TEST_CHECK(notification_queue_current(&queue)->uid == 6U);
    for (int step = 0; step < 4; ++step) {
        TEST_CHECK(notification_queue_older(&queue));
    }
    TEST_CHECK(notification_queue_current(&queue)->uid == 2U);
    TEST_CHECK(!notification_queue_older(&queue));
    TEST_CHECK(notification_queue_current(&queue)->uid == 2U);

    for (int step = 0; step < 4; ++step) {
        TEST_CHECK(notification_queue_newer(&queue));
    }
    TEST_CHECK(notification_queue_current(&queue)->uid == 6U);
    TEST_CHECK(!notification_queue_newer(&queue));
    TEST_CHECK(notification_queue_current(&queue)->uid == 6U);
    return 0;
}
