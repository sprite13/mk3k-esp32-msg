#include "test_check.h"

#include "display_lifecycle.h"

static void test_sleep_and_wake(void)
{
    display_lifecycle_t lifecycle;
    display_lifecycle_init(&lifecycle);

    TEST_CHECK(display_lifecycle_can_render(&lifecycle));
    TEST_CHECK(display_lifecycle_can_sleep(&lifecycle));
    display_lifecycle_mark_sleeping(&lifecycle);
    TEST_CHECK(display_lifecycle_can_render(&lifecycle));
    TEST_CHECK(!display_lifecycle_can_sleep(&lifecycle));
    display_lifecycle_mark_awake(&lifecycle);
    TEST_CHECK(display_lifecycle_can_sleep(&lifecycle));
}

static void test_timeout_is_terminal(void)
{
    display_lifecycle_t lifecycle;
    display_lifecycle_init(&lifecycle);

    display_lifecycle_mark_transfer_timeout(&lifecycle);
    TEST_CHECK(display_lifecycle_failed(&lifecycle));
    TEST_CHECK(!display_lifecycle_can_render(&lifecycle));
    TEST_CHECK(!display_lifecycle_can_sleep(&lifecycle));

    display_lifecycle_mark_awake(&lifecycle);
    display_lifecycle_mark_sleeping(&lifecycle);
    TEST_CHECK(display_lifecycle_failed(&lifecycle));
}

int main(void)
{
    test_sleep_and_wake();
    test_timeout_is_terminal();
    return 0;
}
