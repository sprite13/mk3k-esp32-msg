#include "ancs_advertising.h"

ancs_advertising_policy_t ancs_advertising_policy(void)
{
    return (ancs_advertising_policy_t){
        .service_uuid16 = 0x1811U,
        .include_tx_power = true,
        .include_ancs_solicitation = false,
    };
}
