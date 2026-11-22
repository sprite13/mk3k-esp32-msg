#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t service_uuid16;
    bool include_tx_power;
    bool include_ancs_solicitation;
} ancs_advertising_policy_t;

ancs_advertising_policy_t ancs_advertising_policy(void);
