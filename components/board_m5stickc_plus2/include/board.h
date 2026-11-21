#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

enum {
    BOARD_LCD_SCLK = GPIO_NUM_13,
    BOARD_LCD_MOSI = GPIO_NUM_15,
    BOARD_LCD_CS = GPIO_NUM_5,
    BOARD_LCD_DC = GPIO_NUM_14,
    BOARD_LCD_RST = GPIO_NUM_12,
    BOARD_LCD_BL = GPIO_NUM_27,
    BOARD_BUTTON_A = GPIO_NUM_37,
    BOARD_BUTTON_B = GPIO_NUM_39,
    BOARD_BUTTON_POWER = GPIO_NUM_35,
    BOARD_BUZZER = GPIO_NUM_2,
    BOARD_POWER_HOLD = GPIO_NUM_4,
    BOARD_BATTERY_ADC = GPIO_NUM_38,
};

typedef struct {
    bool a_down;
    bool b_down;
    bool power_down;
} board_buttons_t;

esp_err_t board_power_hold_init(void);
void board_init(void);
void board_backlight_set(bool enabled);
void board_beep(uint32_t frequency_hz, uint32_t duration_ms);
board_buttons_t board_buttons_read(void);
uint32_t board_battery_millivolts(void);
