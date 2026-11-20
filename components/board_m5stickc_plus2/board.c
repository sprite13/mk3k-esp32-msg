#include "board.h"

#include <inttypes.h>
#include <stddef.h>

#include "board_logic.h"
#include "driver/ledc.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

_Static_assert((int)BOARD_LCD_SCLK == (int)GPIO_NUM_13, "LCD SCLK pin changed");
_Static_assert((int)BOARD_LCD_MOSI == (int)GPIO_NUM_15, "LCD MOSI pin changed");
_Static_assert((int)BOARD_LCD_CS == (int)GPIO_NUM_5, "LCD CS pin changed");
_Static_assert((int)BOARD_LCD_DC == (int)GPIO_NUM_14, "LCD DC pin changed");
_Static_assert((int)BOARD_LCD_RST == (int)GPIO_NUM_12, "LCD reset pin changed");
_Static_assert((int)BOARD_LCD_BL == (int)GPIO_NUM_27, "LCD backlight pin changed");
_Static_assert((int)BOARD_BUTTON_A == (int)GPIO_NUM_37, "button A pin changed");
_Static_assert((int)BOARD_BUTTON_B == (int)GPIO_NUM_39, "button B pin changed");
_Static_assert((int)BOARD_BUTTON_POWER == (int)GPIO_NUM_35, "power button pin changed");
_Static_assert((int)BOARD_BUZZER == (int)GPIO_NUM_2, "buzzer pin changed");
_Static_assert((int)BOARD_POWER_HOLD == (int)GPIO_NUM_4, "power hold pin changed");
_Static_assert((int)BOARD_BATTERY_ADC == (int)GPIO_NUM_38, "battery ADC pin changed");

#define BOARD_BUTTON_MASK ((1ULL << BOARD_BUTTON_A) | (1ULL << BOARD_BUTTON_B) | \
                           (1ULL << BOARD_BUTTON_POWER))
#define BOARD_BUZZER_DUTY 512U
#define BOARD_BATTERY_DIVIDER_NUMERATOR 2U

static const char *const TAG = "board";
static const ledc_mode_t BUZZER_SPEED_MODE = LEDC_LOW_SPEED_MODE;
static const ledc_timer_t BUZZER_TIMER = LEDC_TIMER_0;
static const ledc_channel_t BUZZER_CHANNEL = LEDC_CHANNEL_0;

static bool s_initialized;
static esp_timer_handle_t s_buzzer_stop_timer;
static adc_oneshot_unit_handle_t s_adc_handle;
static adc_cali_handle_t s_adc_cali_handle;
static board_debounce_state_t s_button_a;
static board_debounce_state_t s_button_b;
static board_debounce_state_t s_button_power;

static bool button_is_down(gpio_num_t pin)
{
    return gpio_get_level(pin) == 0;
}

static void buzzer_silence(void)
{
    esp_err_t err = ledc_set_duty(BUZZER_SPEED_MODE, BUZZER_CHANNEL, 0U);
    if (err == ESP_OK) {
        err = ledc_update_duty(BUZZER_SPEED_MODE, BUZZER_CHANNEL);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to silence buzzer: %s", esp_err_to_name(err));
    }
}

static void buzzer_stop_callback(void *arg)
{
    (void)arg;
    buzzer_silence();
}

static void buzzer_init(void)
{
    const ledc_timer_config_t timer_config = {
        .speed_mode = BUZZER_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = BUZZER_TIMER,
        .freq_hz = 1000U,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

    const ledc_channel_config_t channel_config = {
        .gpio_num = BOARD_BUZZER,
        .speed_mode = BUZZER_SPEED_MODE,
        .channel = BUZZER_CHANNEL,
        .timer_sel = BUZZER_TIMER,
        .duty = 0U,
        .hpoint = 0,
        .flags.output_invert = 1U,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_config));

    const esp_timer_create_args_t stop_timer_args = {
        .callback = buzzer_stop_callback,
        .name = "buzzer_stop",
    };
    ESP_ERROR_CHECK(esp_timer_create(&stop_timer_args, &s_buzzer_stop_timer));
}

static void adc_init(void)
{
    const adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_config, &s_adc_handle));

    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL_2,
                                                &channel_config));

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    const adc_cali_curve_fitting_config_t calibration_config = {
        .unit_id = ADC_UNIT_1,
        .chan = ADC_CHANNEL_2,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    const esp_err_t calibration_err =
        adc_cali_create_scheme_curve_fitting(&calibration_config, &s_adc_cali_handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_efuse_val_t calibration_source;
    ESP_ERROR_CHECK(adc_cali_scheme_line_fitting_check_efuse(&calibration_source));
    if (calibration_source == ADC_CALI_LINE_FITTING_EFUSE_VAL_DEFAULT_VREF) {
        ESP_LOGW(TAG, "battery ADC calibration unavailable: no eFuse calibration data");
        return;
    }
    const adc_cali_line_fitting_config_t calibration_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .default_vref = 0U,
    };
    const esp_err_t calibration_err =
        adc_cali_create_scheme_line_fitting(&calibration_config, &s_adc_cali_handle);
#else
    const esp_err_t calibration_err = ESP_ERR_NOT_SUPPORTED;
#endif

    if (calibration_err != ESP_OK) {
        s_adc_cali_handle = NULL;
        ESP_LOGW(TAG, "battery ADC calibration unavailable: %s",
                 esp_err_to_name(calibration_err));
    }
}

void board_init(void)
{
    if (s_initialized) {
        return;
    }

    ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)BOARD_POWER_HOLD, 1));
    ESP_ERROR_CHECK(gpio_set_direction((gpio_num_t)BOARD_POWER_HOLD, GPIO_MODE_OUTPUT));

    ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)BOARD_LCD_BL, 0));
    ESP_ERROR_CHECK(gpio_set_direction((gpio_num_t)BOARD_LCD_BL, GPIO_MODE_OUTPUT));

    const gpio_config_t button_config = {
        .pin_bit_mask = BOARD_BUTTON_MASK,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&button_config));

    buzzer_init();
    adc_init();

    const uint64_t now_us = (uint64_t)esp_timer_get_time();
    board_debounce_init(&s_button_a, button_is_down((gpio_num_t)BOARD_BUTTON_A), now_us);
    board_debounce_init(&s_button_b, button_is_down((gpio_num_t)BOARD_BUTTON_B), now_us);
    board_debounce_init(&s_button_power,
                        button_is_down((gpio_num_t)BOARD_BUTTON_POWER), now_us);
    s_initialized = true;
}

void board_backlight_set(bool enabled)
{
    const esp_err_t err =
        gpio_set_level((gpio_num_t)BOARD_LCD_BL, enabled ? 1U : 0U);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to set backlight: %s", esp_err_to_name(err));
    }
}

void board_beep(uint32_t frequency_hz, uint32_t duration_ms)
{
    if (!s_initialized || s_buzzer_stop_timer == NULL) {
        ESP_LOGE(TAG, "cannot beep before board_init");
        return;
    }

    if (esp_timer_is_active(s_buzzer_stop_timer)) {
        const esp_err_t stop_err = esp_timer_stop(s_buzzer_stop_timer);
        if (stop_err != ESP_OK) {
            ESP_LOGW(TAG, "failed to stop previous beep timer: %s",
                     esp_err_to_name(stop_err));
        }
    }

    const uint32_t bounded_duration_ms = board_bound_beep_duration_ms(duration_ms);
    if (frequency_hz == 0U || bounded_duration_ms == 0U) {
        buzzer_silence();
        return;
    }

    esp_err_t err = ledc_set_freq(BUZZER_SPEED_MODE, BUZZER_TIMER, frequency_hz);
    if (err == ESP_OK) {
        err = ledc_set_duty(BUZZER_SPEED_MODE, BUZZER_CHANNEL, BOARD_BUZZER_DUTY);
    }
    if (err == ESP_OK) {
        err = ledc_update_duty(BUZZER_SPEED_MODE, BUZZER_CHANNEL);
    }
    if (err == ESP_OK) {
        const uint64_t duration_us = (uint64_t)bounded_duration_ms * 1000U;
        err = esp_timer_start_once(s_buzzer_stop_timer, duration_us);
    }
    if (err != ESP_OK) {
        buzzer_silence();
        ESP_LOGE(TAG, "failed to start beep: %s", esp_err_to_name(err));
    }
}

board_buttons_t board_buttons_read(void)
{
    if (!s_initialized) {
        return (board_buttons_t){0};
    }

    const uint64_t now_us = (uint64_t)esp_timer_get_time();
    return (board_buttons_t){
        .a_down = board_debounce_update(
            &s_button_a, button_is_down((gpio_num_t)BOARD_BUTTON_A), now_us),
        .b_down = board_debounce_update(
            &s_button_b, button_is_down((gpio_num_t)BOARD_BUTTON_B), now_us),
        .power_down = board_debounce_update(&s_button_power,
                                             button_is_down((gpio_num_t)BOARD_BUTTON_POWER),
                                             now_us),
    };
}

uint32_t board_battery_millivolts(void)
{
    if (!s_initialized || s_adc_handle == NULL || s_adc_cali_handle == NULL) {
        return 0U;
    }

    int raw = 0;
    esp_err_t err = adc_oneshot_read(s_adc_handle, ADC_CHANNEL_2, &raw);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "battery ADC read failed: %s", esp_err_to_name(err));
        return 0U;
    }

    int pin_millivolts = 0;
    err = adc_cali_raw_to_voltage(s_adc_cali_handle, raw, &pin_millivolts);
    if (err != ESP_OK || pin_millivolts < 0) {
        ESP_LOGW(TAG, "battery ADC conversion failed: %s", esp_err_to_name(err));
        return 0U;
    }

    return (uint32_t)pin_millivolts * BOARD_BATTERY_DIVIDER_NUMERATOR;
}
