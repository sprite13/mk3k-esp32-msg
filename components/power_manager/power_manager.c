#include "power_manager.h"

#include <stddef.h>

#ifdef ESP_PLATFORM
#include "esp_pm.h"
#include "sdkconfig.h"
#endif

typedef struct {
    uint16_t millivolts;
    uint8_t percent;
} battery_point_t;

static const battery_point_t BATTERY_POINTS[] = {
    {3300U, 0U},
    {3600U, 20U},
    {3700U, 50U},
    {3850U, 75U},
    {4200U, 100U},
};

uint32_t power_reconnect_delay_ms(uint8_t failures)
{
    static const uint32_t delays[] = {
        1000U, 2000U, 5000U, 10000U, 30000U, 60000U,
    };
    const size_t index = failures < 6U ? failures : 5U;
    return delays[index];
}

uint8_t power_battery_percent(uint32_t millivolts)
{
    if (millivolts <= BATTERY_POINTS[0].millivolts) {
        return BATTERY_POINTS[0].percent;
    }

    for (size_t index = 1U;
         index < sizeof(BATTERY_POINTS) / sizeof(BATTERY_POINTS[0]); ++index) {
        const battery_point_t upper = BATTERY_POINTS[index];
        if (millivolts <= upper.millivolts) {
            const battery_point_t lower = BATTERY_POINTS[index - 1U];
            const uint32_t voltage_offset = millivolts - lower.millivolts;
            const uint32_t voltage_span = upper.millivolts - lower.millivolts;
            const uint32_t percent_span = upper.percent - lower.percent;
            return (uint8_t)(lower.percent +
                             (voltage_offset * percent_span) / voltage_span);
        }
    }

    return BATTERY_POINTS[(sizeof(BATTERY_POINTS) /
                           sizeof(BATTERY_POINTS[0])) - 1U]
        .percent;
}

#ifdef ESP_PLATFORM
esp_err_t power_manager_init(void)
{
    const esp_pm_config_t config = {
        .max_freq_mhz = 240,
        .min_freq_mhz = 80,
#ifdef CONFIG_FREERTOS_USE_TICKLESS_IDLE
        .light_sleep_enable = true,
#else
        .light_sleep_enable = false,
#endif
    };
    return esp_pm_configure(&config);
}
#endif
