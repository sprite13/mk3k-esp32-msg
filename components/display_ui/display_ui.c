#include "display_ui.h"

#include <stdbool.h>

#include "board.h"
#include "display_lifecycle.h"
#include "display_layout.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

enum {
    DISPLAY_PANEL_WIDTH = 135,
    DISPLAY_PANEL_HEIGHT = 240,
    DISPLAY_WIDTH = DISPLAY_PANEL_HEIGHT,
    DISPLAY_HEIGHT = DISPLAY_PANEL_WIDTH,
    DISPLAY_STRIPE_HEIGHT = DISPLAY_FONT_HEIGHT,
    DISPLAY_TOP_MARGIN = 7,
};

#define RGB565(red, green, blue)                                                   \
    (uint16_t)((((uint16_t)(red) & 0xF8U) << 8U) |                                \
               (((uint16_t)(green) & 0xFCU) << 3U) |                              \
               ((uint16_t)(blue) >> 3U))

static const char *const TAG = "display_ui";
static const uint16_t COLOR_BACKGROUND = RGB565(7U, 10U, 14U);
static const uint16_t COLOR_TEXT = RGB565(238U, 242U, 247U);
static const uint16_t COLOR_TITLE = RGB565(255U, 196U, 74U);
static const uint16_t COLOR_SUBTITLE = RGB565(79U, 209U, 197U);
static const uint16_t COLOR_CONNECTED = RGB565(102U, 214U, 139U);
static const uint16_t COLOR_WARNING = RGB565(255U, 183U, 77U);
static const uint16_t COLOR_ERROR = RGB565(255U, 92U, 92U);

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_panel_io;
static StaticSemaphore_t s_transfer_sem_storage;
static SemaphoreHandle_t s_transfer_sem;
static StaticSemaphore_t s_operation_mutex_storage;
static SemaphoreHandle_t s_operation_mutex;
static display_lifecycle_t s_lifecycle = {
    .state = DISPLAY_LIFECYCLE_UNINITIALIZED,
};
static bool s_initialized;
static display_render_plan_t s_plan;
static DMA_ATTR uint16_t s_stripe[DISPLAY_WIDTH * DISPLAY_STRIPE_HEIGHT];

static bool transfer_done(esp_lcd_panel_io_handle_t panel_io,
                          esp_lcd_panel_io_event_data_t *event_data, void *user_ctx)
{
    (void)panel_io;
    (void)event_data;
    BaseType_t task_woken = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)user_ctx, &task_woken);
    return task_woken == pdTRUE;
}

static display_connection_t display_connection(ui_connection_state_t state)
{
    switch (state) {
    case UI_PAIRING:
        return DISPLAY_CONNECTION_PAIRING;
    case UI_CONNECTED:
        return DISPLAY_CONNECTION_CONNECTED;
    case UI_RECONNECTING:
        return DISPLAY_CONNECTION_RECONNECTING;
    case UI_ERROR:
        return DISPLAY_CONNECTION_ERROR;
    default:
        return DISPLAY_CONNECTION_ERROR;
    }
}

static uint16_t line_color(display_line_style_t style, ui_connection_state_t state)
{
    switch (style) {
    case DISPLAY_LINE_WARNING:
        return COLOR_WARNING;
    case DISPLAY_LINE_TITLE:
        return COLOR_TITLE;
    case DISPLAY_LINE_SUBTITLE:
        return COLOR_SUBTITLE;
    case DISPLAY_LINE_BODY:
        return COLOR_TEXT;
    case DISPLAY_LINE_STATUS:
    default:
        return state == UI_CONNECTED ? COLOR_CONNECTED
                                     : (state == UI_ERROR ? COLOR_ERROR : COLOR_WARNING);
    }
}

static void paint_glyph(uint16_t *stripe, size_t x, const uint16_t *rows,
                        uint16_t color)
{
    for (size_t y = 0U; y < DISPLAY_FONT_HEIGHT; ++y) {
        for (size_t pixel = 0U; pixel < DISPLAY_FONT_WIDTH; ++pixel) {
            if ((rows[y] & (uint16_t)(1U << (DISPLAY_FONT_WIDTH - 1U - pixel))) != 0U) {
                stripe[(y * DISPLAY_WIDTH) + x + pixel] = color;
            }
        }
    }
}

static esp_err_t transfer_stripe(int y_start, int y_end)
{
    while (xSemaphoreTake(s_transfer_sem, 0U) == pdTRUE) {
    }

    const esp_err_t err = esp_lcd_panel_draw_bitmap(
        s_panel, 0, y_start, DISPLAY_WIDTH, y_end, s_stripe);
    if (err != ESP_OK) {
        return err;
    }
    if (xSemaphoreTake(s_transfer_sem, pdMS_TO_TICKS(1000U)) == pdTRUE) {
        return ESP_OK;
    }

    /* DMA ownership is indeterminate; reject all panel and stripe reuse. */
    display_lifecycle_mark_transfer_timeout(&s_lifecycle);
    return ESP_ERR_TIMEOUT;
}

static esp_err_t draw_line(size_t row, const display_render_line_t *line,
                           ui_connection_state_t connection)
{
    for (size_t index = 0U; index < DISPLAY_WIDTH * DISPLAY_STRIPE_HEIGHT; ++index) {
        s_stripe[index] = COLOR_BACKGROUND;
    }
    for (size_t column = 0U; column < line->length; ++column) {
        const uint16_t *glyph_rows;
        if (display_font_get_glyph(line->glyphs[column], &glyph_rows)) {
            paint_glyph(s_stripe, column * DISPLAY_FONT_WIDTH, glyph_rows,
                        line_color(line->style, connection));
        }
    }

    const int y_start = DISPLAY_TOP_MARGIN + (int)(row * DISPLAY_FONT_HEIGHT);
    return transfer_stripe(y_start, y_start + DISPLAY_STRIPE_HEIGHT);
}

static esp_err_t fill_margin(int y_start, int y_end)
{
    const size_t pixels = (size_t)(y_end - y_start) * DISPLAY_WIDTH;
    for (size_t index = 0U; index < pixels; ++index) {
        s_stripe[index] = COLOR_BACKGROUND;
    }
    return transfer_stripe(y_start, y_end);
}

esp_err_t display_ui_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_transfer_sem = xSemaphoreCreateBinaryStatic(&s_transfer_sem_storage);
    if (s_transfer_sem == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_operation_mutex = xSemaphoreCreateMutexStatic(&s_operation_mutex_storage);
    if (s_operation_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const spi_bus_config_t bus_config = {
        .mosi_io_num = BOARD_LCD_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .sclk_io_num = BOARD_LCD_SCLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = sizeof(s_stripe),
    };
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        return err;
    }

    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = BOARD_LCD_CS,
        .dc_gpio_num = BOARD_LCD_DC,
        .spi_mode = 0,
        .pclk_hz = 40000000U,
        .trans_queue_depth = 1U,
        .on_color_trans_done = transfer_done,
        .user_ctx = s_transfer_sem,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    esp_lcd_panel_io_handle_t panel_io = NULL;
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config,
                                   &panel_io);
    if (err != ESP_OK) {
        (void)spi_bus_free(SPI2_HOST);
        return err;
    }
    s_panel_io = panel_io;

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BOARD_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16U,
    };
    err = esp_lcd_new_panel_st7789(panel_io, &panel_config, &s_panel);
    if (err == ESP_OK) {
        err = esp_lcd_panel_reset(s_panel);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_init(s_panel);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_swap_xy(s_panel, true);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_mirror(s_panel, true, false);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_set_gap(s_panel, 40, 53);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_invert_color(s_panel, true);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_disp_on_off(s_panel, true);
    }
    if (err != ESP_OK) {
        if (s_panel != NULL) {
            (void)esp_lcd_panel_del(s_panel);
            s_panel = NULL;
        }
        (void)esp_lcd_panel_io_del(s_panel_io);
        s_panel_io = NULL;
        (void)spi_bus_free(SPI2_HOST);
        return err;
    }

    s_initialized = true;
    display_lifecycle_init(&s_lifecycle);
    return ESP_OK;
}

void display_ui_render(const display_ui_model_t *model)
{
    if (!s_initialized || model == NULL ||
        xSemaphoreTake(s_operation_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    if (!display_lifecycle_can_render(&s_lifecycle)) {
        xSemaphoreGive(s_operation_mutex);
        return;
    }
    if (s_lifecycle.state == DISPLAY_LIFECYCLE_SLEEPING) {
        esp_err_t err = esp_lcd_panel_disp_sleep(s_panel, false);
        if (err == ESP_OK) {
            err = esp_lcd_panel_disp_on_off(s_panel, true);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "failed to wake panel: %s", esp_err_to_name(err));
            xSemaphoreGive(s_operation_mutex);
            return;
        }
        display_lifecycle_mark_awake(&s_lifecycle);
    }

    display_render_plan_make(display_connection(model->connection),
                             model->battery_percent, model->position, model->total,
                             model->notification, &s_plan);
    esp_err_t err = fill_margin(0, DISPLAY_TOP_MARGIN);
    for (size_t row = 0U; err == ESP_OK && row < DISPLAY_LAYOUT_MAX_LINES; ++row) {
        err = draw_line(row, &s_plan.lines[row], model->connection);
    }
    if (err == ESP_OK) {
        err = fill_margin(
            DISPLAY_TOP_MARGIN + (DISPLAY_LAYOUT_MAX_LINES * DISPLAY_FONT_HEIGHT),
            DISPLAY_HEIGHT);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "render failed: %s", esp_err_to_name(err));
    }
    xSemaphoreGive(s_operation_mutex);
}

void display_ui_sleep(void)
{
    if (!s_initialized ||
        xSemaphoreTake(s_operation_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    if (!display_lifecycle_can_sleep(&s_lifecycle)) {
        xSemaphoreGive(s_operation_mutex);
        return;
    }
    esp_err_t err = esp_lcd_panel_disp_on_off(s_panel, false);
    if (err == ESP_OK) {
        err = esp_lcd_panel_disp_sleep(s_panel, true);
    }
    if (err == ESP_OK) {
        display_lifecycle_mark_sleeping(&s_lifecycle);
    } else {
        ESP_LOGE(TAG, "failed to sleep panel: %s", esp_err_to_name(err));
    }
    xSemaphoreGive(s_operation_mutex);
}
