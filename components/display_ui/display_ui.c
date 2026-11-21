#include "display_ui.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
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
    DISPLAY_TEXT_ROWS = 6,
    DISPLAY_TOP_MARGIN = 7,
    DISPLAY_COLUMNS = DISPLAY_WIDTH / DISPLAY_FONT_WIDTH,
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

typedef struct {
    uint32_t glyphs[DISPLAY_COLUMNS];
    size_t length;
    uint16_t color;
} rendered_line_t;

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_panel_io;
static StaticSemaphore_t s_transfer_sem_storage;
static SemaphoreHandle_t s_transfer_sem;
static bool s_initialized;
static bool s_sleeping;
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

static const char *state_text(ui_connection_state_t state)
{
    switch (state) {
    case UI_PAIRING:
        return "Сопряжение";
    case UI_CONNECTED:
        return "Подключено";
    case UI_RECONNECTING:
        return "Нет связи";
    case UI_ERROR:
        return "Ошибка";
    default:
        return "Ошибка";
    }
}

static uint16_t state_color(ui_connection_state_t state)
{
    switch (state) {
    case UI_CONNECTED:
        return COLOR_CONNECTED;
    case UI_PAIRING:
    case UI_RECONNECTING:
        return COLOR_WARNING;
    case UI_ERROR:
    default:
        return COLOR_ERROR;
    }
}

static void set_line(rendered_line_t *line, const uint32_t *glyphs, size_t length,
                     uint16_t color)
{
    line->length = length;
    line->color = color;
    if (length > 0U) {
        memcpy(line->glyphs, glyphs, length * sizeof(glyphs[0]));
    }
}

static size_t add_text(rendered_line_t *lines, size_t first_line, size_t max_lines,
                       const char *text, uint16_t color)
{
    if (first_line >= DISPLAY_TEXT_ROWS || max_lines == 0U || text == NULL ||
        text[0] == '\0') {
        return first_line;
    }
    if (max_lines > DISPLAY_TEXT_ROWS - first_line) {
        max_lines = DISPLAY_TEXT_ROWS - first_line;
    }

    display_text_layout_t layout;
    display_layout_text(text, DISPLAY_COLUMNS, max_lines, &layout);
    for (size_t index = 0U; index < layout.line_count; ++index) {
        set_line(&lines[first_line + index], layout.lines[index],
                 layout.line_lengths[index], color);
    }
    return first_line + layout.line_count;
}

static void format_status(const display_ui_model_t *model, char *status,
                          size_t status_size)
{
    if (model->total > 0U) {
        (void)snprintf(status, status_size, "%s %u%% %zu/%zu",
                       state_text(model->connection), (unsigned int)model->battery_percent,
                       model->position, model->total);
    } else {
        (void)snprintf(status, status_size, "%s %u%%", state_text(model->connection),
                       (unsigned int)model->battery_percent);
    }
}

static void make_render_plan(const display_ui_model_t *model,
                             rendered_line_t lines[DISPLAY_TEXT_ROWS])
{
    memset(lines, 0, sizeof(rendered_line_t) * DISPLAY_TEXT_ROWS);
    char status[64];
    format_status(model, status, sizeof(status));
    size_t next = add_text(lines, 0U, 1U, status, state_color(model->connection));

    if (model->connection == UI_CONNECTED && model->notification != NULL) {
        display_content_t content;
        display_content_select(model->notification, &content);
        next = add_text(lines, next, 1U, content.title, COLOR_TITLE);
        next = add_text(lines, next, 1U, content.subtitle, COLOR_SUBTITLE);
        (void)add_text(lines, next, DISPLAY_TEXT_ROWS - next, content.message, COLOR_TEXT);
        return;
    }

    const char *message = "Нет уведомлений";
    if (model->connection == UI_PAIRING) {
        message = "Ожидание iPhone";
    } else if (model->connection == UI_RECONNECTING) {
        message = "Переподключение";
    } else if (model->connection == UI_ERROR) {
        message = "Ошибка соединения";
    }
    (void)add_text(lines, next + 1U, 2U, message, COLOR_TEXT);
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

static esp_err_t draw_line(size_t row, const rendered_line_t *line)
{
    for (size_t index = 0U; index < DISPLAY_WIDTH * DISPLAY_STRIPE_HEIGHT; ++index) {
        s_stripe[index] = COLOR_BACKGROUND;
    }
    for (size_t column = 0U; column < line->length; ++column) {
        const uint16_t *glyph_rows;
        if (display_font_get_glyph(line->glyphs[column], &glyph_rows)) {
            paint_glyph(s_stripe, column * DISPLAY_FONT_WIDTH, glyph_rows, line->color);
        }
    }

    const int y_start = DISPLAY_TOP_MARGIN + (int)(row * DISPLAY_FONT_HEIGHT);
    const esp_err_t err = esp_lcd_panel_draw_bitmap(
        s_panel, 0, y_start, DISPLAY_WIDTH, y_start + DISPLAY_STRIPE_HEIGHT, s_stripe);
    if (err != ESP_OK) {
        return err;
    }
    return xSemaphoreTake(s_transfer_sem, pdMS_TO_TICKS(1000U)) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}

static esp_err_t fill_margin(int y_start, int y_end)
{
    const size_t pixels = (size_t)(y_end - y_start) * DISPLAY_WIDTH;
    for (size_t index = 0U; index < pixels; ++index) {
        s_stripe[index] = COLOR_BACKGROUND;
    }
    const esp_err_t err =
        esp_lcd_panel_draw_bitmap(s_panel, 0, y_start, DISPLAY_WIDTH, y_end, s_stripe);
    if (err != ESP_OK) {
        return err;
    }
    return xSemaphoreTake(s_transfer_sem, pdMS_TO_TICKS(1000U)) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
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
    s_sleeping = false;
    return ESP_OK;
}

void display_ui_render(const display_ui_model_t *model)
{
    if (!s_initialized || model == NULL) {
        return;
    }
    if (s_sleeping) {
        esp_err_t err = esp_lcd_panel_disp_sleep(s_panel, false);
        if (err == ESP_OK) {
            err = esp_lcd_panel_disp_on_off(s_panel, true);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "failed to wake panel: %s", esp_err_to_name(err));
            return;
        }
        s_sleeping = false;
    }

    rendered_line_t lines[DISPLAY_TEXT_ROWS];
    make_render_plan(model, lines);
    esp_err_t err = fill_margin(0, DISPLAY_TOP_MARGIN);
    for (size_t row = 0U; err == ESP_OK && row < DISPLAY_TEXT_ROWS; ++row) {
        err = draw_line(row, &lines[row]);
    }
    if (err == ESP_OK) {
        err = fill_margin(DISPLAY_TOP_MARGIN + (DISPLAY_TEXT_ROWS * DISPLAY_FONT_HEIGHT),
                          DISPLAY_HEIGHT);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "render failed: %s", esp_err_to_name(err));
    }
}

void display_ui_sleep(void)
{
    if (!s_initialized || s_sleeping) {
        return;
    }
    esp_err_t err = esp_lcd_panel_disp_on_off(s_panel, false);
    if (err == ESP_OK) {
        err = esp_lcd_panel_disp_sleep(s_panel, true);
    }
    if (err == ESP_OK) {
        s_sleeping = true;
    } else {
        ESP_LOGE(TAG, "failed to sleep panel: %s", esp_err_to_name(err));
    }
}
