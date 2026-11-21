#pragma once

#include <stdint.h>

uint32_t power_reconnect_delay_ms(uint8_t failures);
uint8_t power_battery_percent(uint32_t millivolts);

#ifdef ESP_PLATFORM
#include "esp_err.h"

esp_err_t power_manager_init(void);
#endif
