#include <stdint.h>

#include "power_manager.h"
#include "test_check.h"

static void test_reconnect_delay_table_and_saturation(void)
{
    static const uint32_t expected[] = {
        1000U, 2000U, 5000U, 10000U, 30000U,
        60000U, 60000U, 60000U, 60000U,
    };

    for (uint8_t failures = 0U;
         failures < (uint8_t)(sizeof(expected) / sizeof(expected[0]));
         ++failures) {
        TEST_CHECK(power_reconnect_delay_ms(failures) == expected[failures]);
    }
}

static void test_battery_percent_table_and_clamping(void)
{
    TEST_CHECK(power_battery_percent(0U) == 0U);
    TEST_CHECK(power_battery_percent(3299U) == 0U);
    TEST_CHECK(power_battery_percent(3300U) == 0U);
    TEST_CHECK(power_battery_percent(3600U) == 20U);
    TEST_CHECK(power_battery_percent(3700U) == 50U);
    TEST_CHECK(power_battery_percent(3850U) == 75U);
    TEST_CHECK(power_battery_percent(4200U) == 100U);
    TEST_CHECK(power_battery_percent(4300U) == 100U);
}

static void test_battery_percent_interpolation_and_monotonicity(void)
{
    TEST_CHECK(power_battery_percent(3450U) == 10U);
    TEST_CHECK(power_battery_percent(3650U) == 35U);
    TEST_CHECK(power_battery_percent(3775U) == 62U);
    TEST_CHECK(power_battery_percent(4025U) == 87U);

    uint8_t previous = power_battery_percent(0U);
    for (uint32_t millivolts = 1U; millivolts <= 5000U; ++millivolts) {
        const uint8_t current = power_battery_percent(millivolts);
        TEST_CHECK(current >= previous);
        TEST_CHECK(current <= 100U);
        previous = current;
    }
}

int main(void)
{
    test_reconnect_delay_table_and_saturation();
    test_battery_percent_table_and_clamping();
    test_battery_percent_interpolation_and_monotonicity();
    return 0;
}
