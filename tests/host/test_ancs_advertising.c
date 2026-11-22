#include "test_check.h"
#include "ancs_advertising.h"

int main(void)
{
    const ancs_advertising_policy_t policy = ancs_advertising_policy();

    TEST_CHECK(policy.service_uuid16 == 0x1811U);
    TEST_CHECK(policy.include_tx_power);
    TEST_CHECK(!policy.include_ancs_solicitation);
    return 0;
}
