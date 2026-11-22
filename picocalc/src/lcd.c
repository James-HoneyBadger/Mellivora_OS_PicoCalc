/*
 * lcd.c — ILI9488 driver for Clockwork PicoCalc
 *
 * Derived from the Clockwork reference driver (lcdspi) and adapted for
 * the Mellivora PicoCalc port.  All hardware constants are in picocalc_hw.h.
 *
 * Chip: ILI9488 (320 × 320, 24-bit BGR, SPI1)
 */

#include "lcd.h"
#include "picocalc_hw.h"
#include "core1.h"

#include "pico/stdlib.h"
#include "pico/mutex.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/dma.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ============================================================
 * Core 1 owns display operations after its service starts. A recursive
 * mutex keeps nested public lcd_* calls and explicit drawing batches atomic.
 * ============================================================ */
static recursive_mutex_t _lcd_mtx;
static bool _lcd_mtx_inited = false;
static uint32_t _lcd_core1_lock_depth;

static inline void _lcd_lock(void) {
    if (_lcd_mtx_inited) recursive_mutex_enter_blocking(&_lcd_mtx);
}
static inline void _lcd_unlock(void) {
    if (_lcd_mtx_inited) recursive_mutex_exit(&_lcd_mtx);
}

typedef enum {
    LCD_OP_LOCK,
    LCD_OP_UNLOCK,
    LCD_OP_FILL,
    LCD_OP_DRAW_PIXEL,
    LCD_OP_DRAW_CHAR,
    LCD_OP_DRAW_STR,
    LCD_OP_CLEAR,
    LCD_OP_CURSOR_ENABLED,
    LCD_OP_CURSOR_IS_BLOCK,
    LCD_OP_CURSOR_ENABLE,
    LCD_OP_CURSOR_TICK,
    LCD_OP_PUTC,
    LCD_OP_PUTS,
    LCD_OP_SPLASH,
    LCD_OP_FILL_RECT,
    LCD_OP_DRAW_HLINE,
    LCD_OP_DRAW_VLINE,
    LCD_OP_DRAW_RECT,
    LCD_OP_SET_FG,
    LCD_OP_SET_BG,
    LCD_OP_GET_FG,
    LCD_OP_GET_BG,
    LCD_OP_SET_CURSOR,
    LCD_OP_GET_COL,
    LCD_OP_GET_ROW,
    LCD_OP_DRAW_CELL,
    LCD_OP_GET_CELL,
    LCD_OP_BLIT_ROW,
} lcd_op_t;

typedef struct {
    lcd_op_t op;
    int i, j;
    int progress;
    uint16_t x, y, w, h;
    uint32_t color, fg, bg, result_color;
    char ch, result_char;
    bool enable, block, result_bool;
    const char *text, *target;
    const uint8_t *pixels;
    int result_int;
} lcd_request_t;

static void _lcd_execute_request(void *context) {
    lcd_request_t *request = (lcd_request_t *)context;
    switch (request->op) {
        case LCD_OP_LOCK: lcd_lock(); break;
        case LCD_OP_UNLOCK: lcd_unlock(); break;
        case LCD_OP_FILL: lcd_fill(request->color); break;
        case LCD_OP_DRAW_PIXEL: lcd_draw_pixel(request->x, request->y, request->color); break;
        case LCD_OP_DRAW_CHAR:
            lcd_draw_char(request->x, request->y, request->ch, request->fg, request->bg);
            break;
        case LCD_OP_DRAW_STR:
            lcd_draw_str(request->x, request->y, request->text, request->fg, request->bg);
            break;
        case LCD_OP_CLEAR: lcd_cls(request->bg); break;
        case LCD_OP_CURSOR_ENABLED: request->result_bool = lcd_cursor_enabled(); break;
        case LCD_OP_CURSOR_IS_BLOCK: request->result_bool = lcd_cursor_is_block(); break;
        case LCD_OP_CURSOR_ENABLE: lcd_cursor_enable(request->enable, request->block); break;
        case LCD_OP_CURSOR_TICK: lcd_cursor_tick(); break;
        case LCD_OP_PUTC: lcd_putc(request->ch); break;
        case LCD_OP_PUTS: lcd_puts(request->text); break;
        case LCD_OP_SPLASH:
            lcd_splash_show(request->text, request->target, request->progress);
            break;
        case LCD_OP_FILL_RECT:
            lcd_fill_rect(request->x, request->y, request->w, request->h, request->color);
            break;
        case LCD_OP_DRAW_HLINE:
            lcd_draw_hline(request->x, request->y, request->w, request->color);
            break;
        case LCD_OP_DRAW_VLINE:
            lcd_draw_vline(request->x, request->y, request->h, request->color);
            break;
        case LCD_OP_DRAW_RECT:
            lcd_draw_rect(request->x, request->y, request->w, request->h, request->color);
            break;
        case LCD_OP_SET_FG: lcd_set_fg(request->color); break;
        case LCD_OP_SET_BG: lcd_set_bg(request->color); break;
        case LCD_OP_GET_FG: request->result_color = lcd_get_fg(); break;
        case LCD_OP_GET_BG: request->result_color = lcd_get_bg(); break;
        case LCD_OP_SET_CURSOR: lcd_set_cursor(request->i, request->j); break;
        case LCD_OP_GET_COL: request->result_int = lcd_get_col(); break;
        case LCD_OP_GET_ROW: request->result_int = lcd_get_row(); break;
        case LCD_OP_DRAW_CELL:
            lcd_draw_cell(request->i, request->j, request->ch, request->fg, request->bg);
            break;
        case LCD_OP_GET_CELL:
            request->result_char = lcd_get_cell(request->i, request->j);
            break;
        case LCD_OP_BLIT_ROW:
            lcd_blit_row(request->x, request->y, request->w, request->pixels);
            break;
    }
}

#define LCD_ROUTE_VOID(op_value, ...) \
    do { \
        if (core1_is_running() && !core1_is_worker()) { \
            lcd_request_t request = { .op = (op_value), __VA_ARGS__ }; \
            core1_call(_lcd_execute_request, &request); \
            return; \
        } \
    } while (0)

#define LCD_ROUTE_VALUE(op_value, result_field, ...) \
    do { \
        if (core1_is_running() && !core1_is_worker()) { \
            lcd_request_t request = { .op = (op_value), __VA_ARGS__ }; \
            core1_call(_lcd_execute_request, &request); \
            return request.result_field; \
        } \
    } while (0)

void lcd_lock(void) {
    LCD_ROUTE_VOID(LCD_OP_LOCK);
    _lcd_lock();
    _lcd_core1_lock_depth++;
}

void lcd_unlock(void) {
    LCD_ROUTE_VOID(LCD_OP_UNLOCK);
    if (_lcd_core1_lock_depth) _lcd_core1_lock_depth--;
    _lcd_unlock();
}

bool lcd_batch_active(void) {
    return _lcd_core1_lock_depth != 0;
}

/* ============================================================
 * DMA acceleration (available on both RP2040 and RP2350)
 * ============================================================ */
static int _dma_chan = -1;
static dma_channel_config _dma_cfg;

/* ============================================================
 * Low-level SPI helpers
 * ============================================================ */

static inline void _dc_high(void) { gpio_put(LCD_PIN_DC, 1); }
static inline void _dc_low(void)  { gpio_put(LCD_PIN_DC, 0); }
static inline void _cs_low(void)  { gpio_put(LCD_PIN_CS, 0); }
static inline void _cs_high(void) { gpio_put(LCD_PIN_CS, 1); }
static inline void _rst_low(void) { gpio_put(LCD_PIN_RST, 0); }
static inline void _rst_high(void){ gpio_put(LCD_PIN_RST, 1); }

static void _spi_write_byte(uint8_t b) {
    spi_write_blocking(LCD_SPI_PORT, &b, 1);
}

static void _spi_write_fast(const uint8_t *src, size_t len) {
    if (!src || len == 0) return;
    /* Use DMA for large transfers (>64 bytes) if channel is initialized */
    if (_dma_chan >= 0 && len > 64) {
        dma_channel_wait_for_finish_blocking(_dma_chan);
        dma_channel_configure(_dma_chan, &_dma_cfg,
                              &spi_get_hw(LCD_SPI_PORT)->dr,
                              src, len, true);
        dma_channel_wait_for_finish_blocking(_dma_chan);
    } else {
        spi_write_blocking(LCD_SPI_PORT, src, len);
    }
}

static void _spi_finish(void) {
    while (spi_is_readable(LCD_SPI_PORT)) {
        (void)spi_get_hw(LCD_SPI_PORT)->dr;
    }
    while (spi_get_hw(LCD_SPI_PORT)->sr & SPI_SSPSR_BSY_BITS) {
        tight_loop_contents();
    }
    while (spi_is_readable(LCD_SPI_PORT)) {
        (void)spi_get_hw(LCD_SPI_PORT)->dr;
    }
    spi_get_hw(LCD_SPI_PORT)->icr = SPI_SSPICR_RORIC_BITS;
}

static void _pin_set_bit(int pin, unsigned int offset) {
    switch (offset) {
        case 5: /* LATCLR */
            gpio_set_pulls(pin, false, false);
            gpio_pull_down(pin);
            gpio_put(pin, 0);
            return;
        case 6: /* LATSET */
            gpio_set_pulls(pin, false, false);
            gpio_pull_up(pin);
            gpio_put(pin, 1);
            return;
        case -3: /* TRISCLR */
            gpio_set_dir(pin, GPIO_OUT);
            gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_12MA);
            sleep_us(2);
            return;
        case -2: /* TRISSET */
            gpio_set_dir(pin, GPIO_IN);
            sleep_us(2);
            return;
        default:
            return;
    }
}

static void _lcd_spi_raise_cs(void) {
    gpio_put(LCD_PIN_CS, 1);
}

static void _lcd_spi_lower_cs(void) {
    gpio_put(LCD_PIN_CS, 0);
}

static void _spi_write_data(uint8_t data) {
    gpio_put(LCD_PIN_DC, 1);
    _lcd_spi_lower_cs();
    _spi_write_byte(data);
    _lcd_spi_raise_cs();
}

static void _spi_write_command(uint8_t data) {
    gpio_put(LCD_PIN_DC, 0);
    _lcd_spi_lower_cs();
    _spi_write_byte(data);
    _lcd_spi_raise_cs();
}

static void _spi_write_cd(uint8_t command, int data, ...) {
    va_list ap;
    va_start(ap, data);
    _spi_write_command(command);
    for (int i = 0; i < data; i++) {
        _spi_write_data((uint8_t)va_arg(ap, int));
    }
    va_end(ap);
}

static void _spi_write_data24(uint32_t data) {
    uint8_t data_array[3];
    data_array[0] = (uint8_t)((data >> 16) & 0xFC);
    data_array[1] = (uint8_t)((data >> 8) & 0xFC);
    data_array[2] = (uint8_t)(data & 0xFC);

    gpio_put(LCD_PIN_DC, 1);
    gpio_put(LCD_PIN_CS, 0);
    spi_write_blocking(LCD_SPI_PORT, data_array, 3);
    gpio_put(LCD_PIN_CS, 1);
}

/* ============================================================
 * Address window
 * ============================================================ */

static void _set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    uint8_t coord[4];
    _lcd_spi_lower_cs();
    gpio_put(LCD_PIN_DC, 0);
    _spi_write_fast(&(uint8_t){0x2A}, 1);
    gpio_put(LCD_PIN_DC, 1);
    coord[0] = x0 >> 8;
    coord[1] = x0;
    coord[2] = x1 >> 8;
    coord[3] = x1;
    _spi_write_fast(coord, 4);

    gpio_put(LCD_PIN_DC, 0);
    _spi_write_fast(&(uint8_t){0x2B}, 1);
    gpio_put(LCD_PIN_DC, 1);
    coord[0] = y0 >> 8;
    coord[1] = y0;
    coord[2] = y1 >> 8;
    coord[3] = y1;
    _spi_write_fast(coord, 4);

    gpio_put(LCD_PIN_DC, 0);
    _spi_write_fast(&(uint8_t){0x2C}, 1);
    gpio_put(LCD_PIN_DC, 1);
}

static void _end_window_write(void) {
    _spi_finish();
    _lcd_spi_raise_cs();
}

static void _reset_controller(void) {
    _pin_set_bit(LCD_PIN_RST, 6);
    sleep_us(10000);
    _pin_set_bit(LCD_PIN_RST, 5);
    sleep_us(10000);
    _pin_set_bit(LCD_PIN_RST, 6);
    sleep_us(200000);
}

/* ============================================================
 * Hardware reset + ILI9488 init sequence
 * ============================================================ */

void lcd_init(void) {
    if (!_lcd_mtx_inited) {
        recursive_mutex_init(&_lcd_mtx);
        _lcd_mtx_inited = true;
    }
    gpio_init(LCD_PIN_SCK);
    gpio_init(LCD_PIN_MOSI);
    gpio_init(LCD_PIN_MISO);
    gpio_init(LCD_PIN_CS);
    gpio_init(LCD_PIN_DC);
    gpio_init(LCD_PIN_RST);

    gpio_set_dir(LCD_PIN_SCK, GPIO_OUT);
    gpio_set_dir(LCD_PIN_MOSI, GPIO_OUT);
    gpio_set_dir(LCD_PIN_CS, GPIO_OUT);
    gpio_set_dir(LCD_PIN_DC, GPIO_OUT);
    gpio_set_dir(LCD_PIN_RST, GPIO_OUT);

    spi_init(LCD_SPI_PORT, LCD_SPI_SPEED);
    spi_set_format(LCD_SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(LCD_PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(LCD_PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(LCD_PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_input_hysteresis_enabled(LCD_PIN_MISO, true);

    gpio_put(LCD_PIN_CS, 1);
    gpio_put(LCD_PIN_RST, 1);

    _reset_controller();

    _spi_write_command(0xE0);
    _spi_write_data(0x00); _spi_write_data(0x03); _spi_write_data(0x09); _spi_write_data(0x08);
    _spi_write_data(0x16); _spi_write_data(0x0A); _spi_write_data(0x3F); _spi_write_data(0x78);
    _spi_write_data(0x4C); _spi_write_data(0x09); _spi_write_data(0x0A); _spi_write_data(0x08);
    _spi_write_data(0x16); _spi_write_data(0x1A); _spi_write_data(0x0F);

    _spi_write_command(0xE1);
    _spi_write_data(0x00); _spi_write_data(0x16); _spi_write_data(0x19); _spi_write_data(0x03);
    _spi_write_data(0x0F); _spi_write_data(0x05); _spi_write_data(0x32); _spi_write_data(0x45);
    _spi_write_data(0x46); _spi_write_data(0x04); _spi_write_data(0x0E); _spi_write_data(0x0D);
    _spi_write_data(0x35); _spi_write_data(0x37); _spi_write_data(0x0F);

    _spi_write_command(0xC0); _spi_write_data(0x17); _spi_write_data(0x15);
    _spi_write_command(0xC1); _spi_write_data(0x41);
    _spi_write_command(0xC5); _spi_write_data(0x00); _spi_write_data(0x12); _spi_write_data(0x80);
    _spi_write_command(0x36); _spi_write_data(0x48);
    _spi_write_command(0x3A); _spi_write_data(0x66);
    _spi_write_command(0xB0); _spi_write_data(0x00);
    _spi_write_command(0xB1); _spi_write_data(0xA0);
    _spi_write_command(0x21);
    _spi_write_command(0xB4); _spi_write_data(0x02);
    _spi_write_command(0xB6); _spi_write_data(0x02); _spi_write_data(0x02); _spi_write_data(0x3B);
    _spi_write_command(0xB7); _spi_write_data(0xC6);
    _spi_write_command(0xE9); _spi_write_data(0x00);
    _spi_write_command(0xF7); _spi_write_data(0xA9); _spi_write_data(0x51); _spi_write_data(0x2C); _spi_write_data(0x82);

    _spi_write_command(0x11);
    sleep_ms(120);
    _spi_write_command(0x29);
    sleep_ms(120);
    _spi_write_cd(0x36, 1, 0x48);

    /* Initialize DMA channel for accelerated SPI writes */
    _dma_chan = (int)dma_claim_unused_channel(false);
    if (_dma_chan >= 0) {
        _dma_cfg = dma_channel_get_default_config((uint)_dma_chan);
        channel_config_set_transfer_data_size(&_dma_cfg, DMA_SIZE_8);
        channel_config_set_dreq(&_dma_cfg, spi_get_dreq(LCD_SPI_PORT, true));
        channel_config_set_read_increment(&_dma_cfg, true);
        channel_config_set_write_increment(&_dma_cfg, false);
    }
}

/* ============================================================
 * Drawing primitives
 * ============================================================ */

void lcd_fill(uint32_t color) {
    LCD_ROUTE_VOID(LCD_OP_FILL, .color = color);
    _lcd_lock();
    uint8_t r = (uint8_t)((color >> 16) & 0xFC);
    uint8_t g = (uint8_t)((color >>  8) & 0xFC);
    uint8_t b = (uint8_t)(color & 0xFC);

    static uint8_t linebuf[LCD_WIDTH * 3];
    for (int x = 0; x < LCD_WIDTH; x++) {
        linebuf[x * 3 + 0] = r;
        linebuf[x * 3 + 1] = g;
        linebuf[x * 3 + 2] = b;
    }

    _set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
    for (int y = 0; y < LCD_HEIGHT; y++) {
        _spi_write_fast(linebuf, sizeof linebuf);
    }
    _end_window_write();
    _lcd_unlock();
}

void lcd_draw_pixel(uint16_t x, uint16_t y, uint32_t color) {
    LCD_ROUTE_VOID(LCD_OP_DRAW_PIXEL, .x = x, .y = y, .color = color);
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
    _lcd_lock();
    _set_window(x, y, x, y);
    _spi_write_data24(color);
    _lcd_unlock();
}

/* ============================================================
 * 8×16 built-in font  (ISO 8859-1 printable range 0x20-0x7E)
 * ============================================================ */

/* Minimal 8×16 pixel font — each character is 16 bytes (1 byte per row).
 * Only printable ASCII (0x20-0x7E).  Sourced from public-domain VGA font data. */
static const uint8_t _font8x16[95][16] = {
    /* 0x20 space */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x21 ! */
    {0x00,0x00,0x18,0x3C,0x3C,0x3C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
    /* 0x22 " */
    {0x00,0x00,0x66,0x66,0x66,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x23 # */
    {0x00,0x00,0x36,0x36,0x7F,0x36,0x36,0x36,0x7F,0x36,0x36,0x00,0x00,0x00,0x00,0x00},
    /* 0x24 $ */
    {0x00,0x18,0x7E,0xDB,0xD8,0xD8,0x7E,0x1B,0x1B,0xDB,0x7E,0x18,0x00,0x00,0x00,0x00},
    /* 0x25 % */
    {0x00,0x00,0x00,0x62,0x66,0x0C,0x18,0x30,0x66,0x46,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x26 & */
    {0x00,0x00,0x38,0x6C,0x6C,0x38,0x76,0xDC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00,0x00},
    /* 0x27 ' */
    {0x00,0x00,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x28 ( */
    {0x00,0x00,0x0C,0x18,0x30,0x30,0x30,0x30,0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00},
    /* 0x29 ) */
    {0x00,0x00,0x30,0x18,0x0C,0x0C,0x0C,0x0C,0x0C,0x18,0x30,0x00,0x00,0x00,0x00,0x00},
    /* 0x2A * */
    {0x00,0x00,0x00,0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x2B + */
    {0x00,0x00,0x00,0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x2C , */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00},
    /* 0x2D - */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x2E . */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
    /* 0x2F / */
    {0x00,0x00,0x03,0x06,0x0C,0x18,0x30,0x60,0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x30 0 */
    {0x00,0x00,0x3C,0x66,0xC3,0xC3,0xDB,0xDB,0xC3,0xC3,0x66,0x3C,0x00,0x00,0x00,0x00},
    /* 0x31 1 */
    {0x00,0x00,0x18,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00,0x00,0x00},
    /* 0x32 2 */
    {0x00,0x00,0x7E,0xC3,0x03,0x06,0x0C,0x18,0x30,0x60,0xFF,0x00,0x00,0x00,0x00,0x00},
    /* 0x33 3 */
    {0x00,0x00,0xFF,0x06,0x0C,0x3E,0x06,0x03,0x03,0xC3,0x7E,0x00,0x00,0x00,0x00,0x00},
    /* 0x34 4 */
    {0x00,0x00,0x06,0x0E,0x1E,0x36,0x66,0xC6,0xFF,0x06,0x06,0x00,0x00,0x00,0x00,0x00},
    /* 0x35 5 */
    {0x00,0x00,0xFF,0xC0,0xC0,0xFE,0x03,0x03,0x03,0xC3,0x7E,0x00,0x00,0x00,0x00,0x00},
    /* 0x36 6 */
    {0x00,0x00,0x3C,0x60,0xC0,0xFE,0xC3,0xC3,0xC3,0xC3,0x7E,0x00,0x00,0x00,0x00,0x00},
    /* 0x37 7 */
    {0x00,0x00,0xFF,0x03,0x06,0x0C,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
    /* 0x38 8 */
    {0x00,0x00,0x7E,0xC3,0xC3,0xC3,0x7E,0xC3,0xC3,0xC3,0x7E,0x00,0x00,0x00,0x00,0x00},
    /* 0x39 9 */
    {0x00,0x00,0x7E,0xC3,0xC3,0xC3,0x7F,0x03,0x03,0x06,0x3C,0x00,0x00,0x00,0x00,0x00},
    /* 0x3A : */
    {0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x3B ; */
    {0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00},
    /* 0x3C < */
    {0x00,0x00,0x06,0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x06,0x00,0x00,0x00,0x00,0x00},
    /* 0x3D = */
    {0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x3E > */
    {0x00,0x00,0x60,0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x60,0x00,0x00,0x00,0x00,0x00},
    /* 0x3F ? */
    {0x00,0x00,0x7E,0xC3,0x03,0x06,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
    /* 0x40 @ */
    {0x00,0x00,0x7E,0xC3,0xCF,0xDB,0xDB,0xDF,0xC0,0xC0,0x7F,0x00,0x00,0x00,0x00,0x00},
    /* 0x41 A */
    {0x00,0x00,0x18,0x3C,0x66,0xC3,0xC3,0xFF,0xC3,0xC3,0xC3,0x00,0x00,0x00,0x00,0x00},
    /* 0x42 B */
    {0x00,0x00,0xFE,0xC3,0xC3,0xC3,0xFE,0xC3,0xC3,0xC3,0xFE,0x00,0x00,0x00,0x00,0x00},
    /* 0x43 C */
    {0x00,0x00,0x3C,0x66,0xC3,0xC0,0xC0,0xC0,0xC3,0x66,0x3C,0x00,0x00,0x00,0x00,0x00},
    /* 0x44 D */
    {0x00,0x00,0xFC,0xC6,0xC3,0xC3,0xC3,0xC3,0xC3,0xC6,0xFC,0x00,0x00,0x00,0x00,0x00},
    /* 0x45 E */
    {0x00,0x00,0xFF,0xC0,0xC0,0xC0,0xFC,0xC0,0xC0,0xC0,0xFF,0x00,0x00,0x00,0x00,0x00},
    /* 0x46 F */
    {0x00,0x00,0xFF,0xC0,0xC0,0xC0,0xFC,0xC0,0xC0,0xC0,0xC0,0x00,0x00,0x00,0x00,0x00},
    /* 0x47 G */
    {0x00,0x00,0x3C,0x66,0xC3,0xC0,0xCF,0xC3,0xC3,0x66,0x3C,0x00,0x00,0x00,0x00,0x00},
    /* 0x48 H */
    {0x00,0x00,0xC3,0xC3,0xC3,0xC3,0xFF,0xC3,0xC3,0xC3,0xC3,0x00,0x00,0x00,0x00,0x00},
    /* 0x49 I */
    {0x00,0x00,0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00,0x00,0x00},
    /* 0x4A J */
    {0x00,0x00,0x3E,0x0C,0x0C,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0x00,0x00,0x00,0x00,0x00},
    /* 0x4B K */
    {0x00,0x00,0xC3,0xC6,0xCC,0xD8,0xF0,0xD8,0xCC,0xC6,0xC3,0x00,0x00,0x00,0x00,0x00},
    /* 0x4C L */
    {0x00,0x00,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xFF,0x00,0x00,0x00,0x00,0x00},
    /* 0x4D M */
    {0x00,0x00,0xC3,0xE7,0xFF,0xDB,0xC3,0xC3,0xC3,0xC3,0xC3,0x00,0x00,0x00,0x00,0x00},
    /* 0x4E N */
    {0x00,0x00,0xC3,0xE3,0xF3,0xDB,0xCF,0xC7,0xC3,0xC3,0xC3,0x00,0x00,0x00,0x00,0x00},
    /* 0x4F O */
    {0x00,0x00,0x7E,0xC3,0xC3,0xC3,0xC3,0xC3,0xC3,0xC3,0x7E,0x00,0x00,0x00,0x00,0x00},
    /* 0x50 P */
    {0x00,0x00,0xFE,0xC3,0xC3,0xC3,0xFE,0xC0,0xC0,0xC0,0xC0,0x00,0x00,0x00,0x00,0x00},
    /* 0x51 Q */
    {0x00,0x00,0x7E,0xC3,0xC3,0xC3,0xC3,0xDB,0xCF,0xC6,0x7D,0x00,0x00,0x00,0x00,0x00},
    /* 0x52 R */
    {0x00,0x00,0xFE,0xC3,0xC3,0xC3,0xFE,0xD8,0xCC,0xC6,0xC3,0x00,0x00,0x00,0x00,0x00},
    /* 0x53 S */
    {0x00,0x00,0x7E,0xC3,0xC0,0xC0,0x7E,0x03,0x03,0xC3,0x7E,0x00,0x00,0x00,0x00,0x00},
    /* 0x54 T */
    {0x00,0x00,0xFF,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
    /* 0x55 U */
    {0x00,0x00,0xC3,0xC3,0xC3,0xC3,0xC3,0xC3,0xC3,0xC3,0x7E,0x00,0x00,0x00,0x00,0x00},
    /* 0x56 V */
    {0x00,0x00,0xC3,0xC3,0xC3,0x66,0x66,0x3C,0x3C,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
    /* 0x57 W */
    {0x00,0x00,0xC3,0xC3,0xC3,0xDB,0xDB,0xFF,0x66,0x66,0x66,0x00,0x00,0x00,0x00,0x00},
    /* 0x58 X */
    {0x00,0x00,0xC3,0xC3,0x66,0x3C,0x18,0x3C,0x66,0xC3,0xC3,0x00,0x00,0x00,0x00,0x00},
    /* 0x59 Y */
    {0x00,0x00,0xC3,0xC3,0x66,0x3C,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
    /* 0x5A Z */
    {0x00,0x00,0xFF,0x03,0x06,0x0C,0x18,0x30,0x60,0xC0,0xFF,0x00,0x00,0x00,0x00,0x00},
    /* 0x5B [ */
    {0x00,0x00,0x3C,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x3C,0x00,0x00,0x00,0x00,0x00},
    /* 0x5C \ */
    {0x00,0x00,0xC0,0x60,0x30,0x18,0x0C,0x06,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x5D ] */
    {0x00,0x00,0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00,0x00,0x00,0x00,0x00},
    /* 0x5E ^ */
    {0x00,0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x5F _ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0x00,0x00,0x00},
    /* 0x60 ` */
    {0x00,0x30,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x61-0x7A lowercase — reuse uppercase shape shifted */
    /* 0x61 a */
    {0x00,0x00,0x00,0x00,0x3C,0x06,0x3E,0x66,0x66,0x66,0x3B,0x00,0x00,0x00,0x00,0x00},
    /* 0x62 b */
    {0x00,0x00,0xC0,0xC0,0xDC,0xE6,0xC3,0xC3,0xC3,0xE6,0xDC,0x00,0x00,0x00,0x00,0x00},
    /* 0x63 c */
    {0x00,0x00,0x00,0x00,0x7E,0xC3,0xC0,0xC0,0xC3,0x66,0x3C,0x00,0x00,0x00,0x00,0x00},
    /* 0x64 d */
    {0x00,0x00,0x03,0x03,0x3F,0x63,0xC3,0xC3,0xC3,0x63,0x3B,0x00,0x00,0x00,0x00,0x00},
    /* 0x65 e */
    {0x00,0x00,0x00,0x00,0x7E,0xC3,0xFF,0xC0,0xC0,0xC3,0x7E,0x00,0x00,0x00,0x00,0x00},
    /* 0x66 f */
    {0x00,0x00,0x1E,0x30,0x30,0x7C,0x30,0x30,0x30,0x30,0x30,0x00,0x00,0x00,0x00,0x00},
    /* 0x67 g */
    {0x00,0x00,0x3B,0x63,0xC3,0xC3,0x63,0x3F,0x03,0x03,0x7E,0x00,0x00,0x00,0x00,0x00},
    /* 0x68 h */
    {0x00,0x00,0xC0,0xC0,0xDC,0xE6,0xC3,0xC3,0xC3,0xC3,0xC3,0x00,0x00,0x00,0x00,0x00},
    /* 0x69 i */
    {0x00,0x00,0x18,0x00,0x78,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00,0x00,0x00},
    /* 0x6A j */
    {0x00,0x00,0x06,0x00,0x1E,0x06,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00},
    /* 0x6B k */
    {0x00,0x00,0xC0,0xC0,0xC6,0xCC,0xD8,0xF0,0xD8,0xCC,0xC6,0x00,0x00,0x00,0x00,0x00},
    /* 0x6C l */
    {0x00,0x00,0x78,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00,0x00,0x00},
    /* 0x6D m */
    {0x00,0x00,0x00,0x00,0xE6,0xFF,0xDB,0xC3,0xC3,0xC3,0xC3,0x00,0x00,0x00,0x00,0x00},
    /* 0x6E n */
    {0x00,0x00,0x00,0x00,0xDC,0xE6,0xC3,0xC3,0xC3,0xC3,0xC3,0x00,0x00,0x00,0x00,0x00},
    /* 0x6F o */
    {0x00,0x00,0x00,0x00,0x7E,0xC3,0xC3,0xC3,0xC3,0xC3,0x7E,0x00,0x00,0x00,0x00,0x00},
    /* 0x70 p */
    {0x00,0x00,0x00,0x00,0xDC,0xE6,0xC3,0xC3,0xE6,0xDC,0xC0,0x00,0x00,0x00,0x00,0x00},
    /* 0x71 q */
    {0x00,0x00,0x00,0x00,0x3B,0x63,0xC3,0xC3,0x63,0x3F,0x03,0x00,0x00,0x00,0x00,0x00},
    /* 0x72 r */
    {0x00,0x00,0x00,0x00,0xDE,0x70,0x60,0x60,0x60,0x60,0x60,0x00,0x00,0x00,0x00,0x00},
    /* 0x73 s */
    {0x00,0x00,0x00,0x00,0x7E,0xC3,0x60,0x3C,0x06,0xC3,0x7E,0x00,0x00,0x00,0x00,0x00},
    /* 0x74 t */
    {0x00,0x00,0x18,0x18,0x7E,0x18,0x18,0x18,0x18,0x18,0x0E,0x00,0x00,0x00,0x00,0x00},
    /* 0x75 u */
    {0x00,0x00,0x00,0x00,0xC3,0xC3,0xC3,0xC3,0xC3,0x63,0x3B,0x00,0x00,0x00,0x00,0x00},
    /* 0x76 v */
    {0x00,0x00,0x00,0x00,0xC3,0xC3,0x66,0x66,0x3C,0x3C,0x18,0x00,0x00,0x00,0x00,0x00},
    /* 0x77 w */
    {0x00,0x00,0x00,0x00,0xC3,0xC3,0xDB,0xDB,0xFF,0x66,0x66,0x00,0x00,0x00,0x00,0x00},
    /* 0x78 x */
    {0x00,0x00,0x00,0x00,0xC3,0x66,0x3C,0x18,0x3C,0x66,0xC3,0x00,0x00,0x00,0x00,0x00},
    /* 0x79 y */
    {0x00,0x00,0x00,0x00,0xC3,0xC3,0xC3,0x7F,0x03,0xC3,0x7E,0x00,0x00,0x00,0x00,0x00},
    /* 0x7A z */
    {0x00,0x00,0x00,0x00,0xFF,0x06,0x0C,0x18,0x30,0x60,0xFF,0x00,0x00,0x00,0x00,0x00},
    /* 0x7B { */
    {0x00,0x00,0x0E,0x18,0x18,0x18,0x70,0x18,0x18,0x18,0x0E,0x00,0x00,0x00,0x00,0x00},
    /* 0x7C | */
    {0x00,0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00},
    /* 0x7D } */
    {0x00,0x00,0x70,0x18,0x18,0x18,0x0E,0x18,0x18,0x18,0x70,0x00,0x00,0x00,0x00,0x00},
    /* 0x7E ~ */
    {0x00,0x00,0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

void lcd_draw_char(uint16_t x, uint16_t y, char c, uint32_t fg, uint32_t bg) {
    LCD_ROUTE_VOID(LCD_OP_DRAW_CHAR,
        .x = x, .y = y, .ch = c, .fg = fg, .bg = bg);
    if ((uint8_t)c < 0x20 || (uint8_t)c > 0x7E) c = '?';
    const uint8_t *glyph = _font8x16[(uint8_t)c - 0x20];
    _lcd_lock();

    uint8_t fr = (uint8_t)((fg >> 16) & 0xFC);
    uint8_t fgc = (uint8_t)((fg >> 8) & 0xFC);
    uint8_t fb = (uint8_t)(fg & 0xFC);
    uint8_t br = (uint8_t)((bg >> 16) & 0xFC);
    uint8_t bgc = (uint8_t)((bg >> 8) & 0xFC);
    uint8_t bb = (uint8_t)(bg & 0xFC);

    uint8_t cell[LCD_CHAR_W * LCD_CHAR_H * 3];
    size_t idx = 0;
    for (int row = 0; row < LCD_CHAR_H; row++) {
        uint8_t bits = glyph[row];
        for (int bit = 0; bit < LCD_CHAR_W; bit++) {
            bool on = (bits & (0x80u >> bit)) != 0;
            cell[idx++] = on ? fr : br;
            cell[idx++] = on ? fgc : bgc;
            cell[idx++] = on ? fb : bb;
        }
    }

    _set_window(x, y, (uint16_t)(x + LCD_CHAR_W - 1), (uint16_t)(y + LCD_CHAR_H - 1));
    _spi_write_fast(cell, sizeof cell);
    _end_window_write();
    _lcd_unlock();
}

void lcd_draw_str(uint16_t x, uint16_t y, const char *s, uint32_t fg, uint32_t bg) {
    LCD_ROUTE_VOID(LCD_OP_DRAW_STR,
        .x = x, .y = y, .text = s, .fg = fg, .bg = bg);
    _lcd_lock();
    while (*s) {
        lcd_draw_char(x, y, *s++, fg, bg);
        x += LCD_CHAR_W;
        if (x + LCD_CHAR_W > LCD_WIDTH) break;
    }
    _lcd_unlock();
}

/* ============================================================
 * Scroll + terminal cursor
 * ============================================================ */

static uint32_t _fg = LCD_GREEN;
static uint32_t _bg = LCD_BLACK;
static int _cur_col = 0;
static int _cur_row = 0;
static char     _text_buf[LCD_ROWS][LCD_COLS];
static uint32_t _color_buf[LCD_ROWS][LCD_COLS]; /* per-character foreground color */

/* Cursor blink state */
static bool _cursor_enabled = false;
static bool _cursor_block    = true;
static bool _cursor_visible  = false;
static uint32_t _cursor_last_ms = 0;
static int _cursor_last_col = -1;
static int _cursor_last_row = -1;

static void _lcd_cursor_render(bool visible);
static void _lcd_cursor_erase_last(void);

static void _clear_text_buf(void) {
    for (int r = 0; r < LCD_ROWS; r++) {
        for (int c = 0; c < LCD_COLS; c++) {
            _text_buf[r][c]  = ' ';
            _color_buf[r][c] = _fg;
        }
    }
}

/* Scroll entire screen up by one text row */
static void _scroll_up(void) {
    memmove(_text_buf[0],  _text_buf[1],  (LCD_ROWS - 1) * LCD_COLS * sizeof(char));
    memmove(_color_buf[0], _color_buf[1], (LCD_ROWS - 1) * LCD_COLS * sizeof(uint32_t));
    for (int c = 0; c < LCD_COLS; c++) {
        _text_buf[LCD_ROWS - 1][c]  = ' ';
        _color_buf[LCD_ROWS - 1][c] = _fg;
    }
    _cur_col = 0;
    _cur_row = LCD_ROWS - 1;

    /* Rebuild the entire screen using per-character stored colors */
    uint8_t br  = (uint8_t)((_bg >> 16) & 0xFC);
    uint8_t bgc = (uint8_t)((_bg >> 8)  & 0xFC);
    uint8_t bb  = (uint8_t)(_bg & 0xFC);

    /* Write one row at a time but keep the same SPI window open */
    _set_window(0, 0, LCD_WIDTH - 1, (uint16_t)(LCD_ROWS * LCD_CHAR_H - 1));
    static uint8_t rowbuf[LCD_WIDTH * LCD_CHAR_H * 3];
    for (int row = 0; row < LCD_ROWS; row++) {
        size_t idx = 0;
        for (int glyph_row = 0; glyph_row < LCD_CHAR_H; glyph_row++) {
            for (int col = 0; col < LCD_COLS; col++) {
                uint32_t cfg = _color_buf[row][col];
                uint8_t fr  = (uint8_t)((cfg >> 16) & 0xFC);
                uint8_t fgc = (uint8_t)((cfg >> 8)  & 0xFC);
                uint8_t fb  = (uint8_t)(cfg & 0xFC);
                char ch = _text_buf[row][col];
                if ((uint8_t)ch < 0x20 || (uint8_t)ch > 0x7E) ch = ' ';
                const uint8_t *glyph = _font8x16[(uint8_t)ch - 0x20];
                uint8_t bits = glyph[glyph_row];
                for (int bit = 0; bit < LCD_CHAR_W; bit++) {
                    bool on = (bits & (0x80u >> bit)) != 0;
                    rowbuf[idx++] = on ? fr : br;
                    rowbuf[idx++] = on ? fgc : bgc;
                    rowbuf[idx++] = on ? fb : bb;
                }
            }
        }
        _spi_write_fast(rowbuf, idx);
    }
    _end_window_write();
}

void lcd_cls(uint32_t bg) {
    LCD_ROUTE_VOID(LCD_OP_CLEAR, .bg = bg);
    _lcd_lock();
    _bg = bg;
    _clear_text_buf();
    lcd_fill(bg);
    _cur_col = 0;
    _cur_row = 0;
    _cursor_last_col = -1;
    _cursor_last_row = -1;
    if (_cursor_enabled) _lcd_cursor_render(true);
    _lcd_unlock();
}

/* Draw or erase the blinking cursor at (_cur_col, _cur_row). */
static void _lcd_cursor_render(bool visible) {
    if (_cur_row < 0 || _cur_row >= LCD_ROWS || _cur_col < 0 || _cur_col >= LCD_COLS) return;
    char ch = _text_buf[_cur_row][_cur_col];
    if ((uint8_t)ch < 0x20 || (uint8_t)ch > 0x7E) ch = ' ';
    uint32_t cfg = _color_buf[_cur_row][_cur_col];
    uint16_t px = (uint16_t)(_cur_col * LCD_CHAR_W);
    uint16_t py = (uint16_t)(_cur_row * LCD_CHAR_H);
    if (visible) {
        if (_cursor_block) {
            lcd_draw_char(px, py, ch, _bg, cfg);
        } else {
            /* Underscore cursor: draw '_' in foreground color. */
            lcd_draw_char(px, py, '_', cfg, _bg);
        }
    } else {
        lcd_draw_char(px, py, ch, cfg, _bg);
    }
    _cursor_last_col = _cur_col;
    _cursor_last_row = _cur_row;
}

static void _lcd_cursor_erase_last(void) {
    if (_cursor_last_row < 0 || _cursor_last_row >= LCD_ROWS ||
        _cursor_last_col < 0 || _cursor_last_col >= LCD_COLS) return;
    char ch = _text_buf[_cursor_last_row][_cursor_last_col];
    if ((uint8_t)ch < 0x20 || (uint8_t)ch > 0x7E) ch = ' ';
    uint32_t cfg = _color_buf[_cursor_last_row][_cursor_last_col];
    lcd_draw_char((uint16_t)(_cursor_last_col * LCD_CHAR_W),
                  (uint16_t)(_cursor_last_row * LCD_CHAR_H),
                  ch, cfg, _bg);
    _cursor_last_col = -1;
    _cursor_last_row = -1;
}

bool lcd_cursor_enabled(void) {
    LCD_ROUTE_VALUE(LCD_OP_CURSOR_ENABLED, result_bool);
    return _cursor_enabled;
}

bool lcd_cursor_is_block(void) {
    LCD_ROUTE_VALUE(LCD_OP_CURSOR_IS_BLOCK, result_bool);
    return _cursor_block;
}

void lcd_cursor_enable(bool enable, bool block) {
    LCD_ROUTE_VOID(LCD_OP_CURSOR_ENABLE, .enable = enable, .block = block);
    _lcd_lock();
    if (_cursor_enabled && !enable) {
        _lcd_cursor_erase_last();
    }
    _cursor_enabled = enable;
    _cursor_block = block;
    _cursor_visible = enable;
    _cursor_last_ms = (uint32_t)to_ms_since_boot(get_absolute_time());
    if (enable) _lcd_cursor_render(true);
    _lcd_unlock();
}

void lcd_cursor_tick(void) {
    LCD_ROUTE_VOID(LCD_OP_CURSOR_TICK);
    if (!_cursor_enabled) return;
    uint32_t now = (uint32_t)to_ms_since_boot(get_absolute_time());
    if (now - _cursor_last_ms < 500) return;
    _cursor_last_ms = now;
    _lcd_lock();
    _cursor_visible = !_cursor_visible;
    _lcd_cursor_render(_cursor_visible);
    _lcd_unlock();
}

void lcd_putc(char c) {
    LCD_ROUTE_VOID(LCD_OP_PUTC, .ch = c);
    _lcd_lock();
    if (_cursor_enabled) _lcd_cursor_render(false);
    if (c == '\n') {
        _cur_col = 0;
        _cur_row++;
        if (_cur_row >= LCD_ROWS) _scroll_up();
        _lcd_unlock();
        return;
    }
    if (c == '\r') {
        _cur_col = 0;
        _lcd_unlock();
        return;
    }
    uint16_t py = (uint16_t)(_cur_row * LCD_CHAR_H);

    if (c == '\b') {
        if (_cur_col > 0) {
            _cur_col--;
            _text_buf[_cur_row][_cur_col]  = ' ';
            _color_buf[_cur_row][_cur_col] = _fg;
            lcd_draw_char((uint16_t)(_cur_col * LCD_CHAR_W), py, ' ', _fg, _bg);
        }
        _lcd_unlock();
        return;
    }
    if ((uint8_t)c < 0x20 || (uint8_t)c > 0x7E) c = '?';
    _text_buf[_cur_row][_cur_col]  = c;
    _color_buf[_cur_row][_cur_col] = _fg;
    lcd_draw_char((uint16_t)(_cur_col * LCD_CHAR_W), py, c, _fg, _bg);
    _cur_col++;
    if (_cur_col >= LCD_COLS) {
        _cur_col = 0;
        _cur_row++;
        if (_cur_row >= LCD_ROWS) _scroll_up();
    }
    if (_cursor_enabled) _lcd_cursor_render(true);
    _lcd_unlock();
}

void lcd_puts(const char *s) {
    LCD_ROUTE_VOID(LCD_OP_PUTS, .text = s);
    _lcd_lock();
    while (*s) lcd_putc(*s++);
    _lcd_unlock();
}

/* ============================================================
 * Boot splash
 * ============================================================ */

void lcd_splash_show(const char *version, const char *target, int progress_pct) {
    LCD_ROUTE_VOID(LCD_OP_SPLASH,
        .text = version, .target = target, .progress = progress_pct);
    _lcd_lock();
    uint32_t fg = LCD_AMBER;
    uint32_t bg = LCD_BLACK;
    _fg = fg;
    _bg = bg;
    _clear_text_buf();
    lcd_fill(bg);
    _cur_col = 0;
    _cur_row = 0;

    /* Decorative top rule */
    lcd_set_cursor(0, 1);
    for (int i = 0; i < LCD_COLS; i++) lcd_putc('=');

    /* Title */
    const char *title = "Mellivora OS";
    int tlen = (int)strlen(title);
    lcd_set_cursor((LCD_COLS - tlen) / 2, 3);
    lcd_puts(title);

    /* Version / target */
    char line[LCD_COLS + 1];
    snprintf(line, sizeof line, "v%s  [%s]", version ? version : "", target ? target : "Pico");
    int llen = (int)strlen(line);
    lcd_set_cursor((LCD_COLS - llen) / 2, 5);
    lcd_puts(line);

    /* Simple honeycomb-ish motif */
    const char *motif[] = {
        "  ##  ##  ",
        " #  ##  # ",
        "#        #",
        " #  ##  # ",
        "  ##  ##  ",
    };
    int motif_rows = sizeof motif / sizeof motif[0];
    int motif_start = (LCD_ROWS - motif_rows) / 2 - 1;
    for (int r = 0; r < motif_rows; r++) {
        int mlen = (int)strlen(motif[r]);
        lcd_set_cursor((LCD_COLS - mlen) / 2, motif_start + r);
        lcd_puts(motif[r]);
    }

    /* Status line */
    lcd_set_cursor(0, LCD_ROWS - 3);
    for (int i = 0; i < LCD_COLS; i++) lcd_putc('-');

    lcd_set_cursor(2, LCD_ROWS - 2);
    lcd_puts("Booting...");

    /* Progress bar: 36 chars wide, centered-ish */
    int bar_w = 36;
    int filled = progress_pct < 0 ? 0 : progress_pct > 100 ? bar_w : (progress_pct * bar_w) / 100;
    lcd_set_cursor(2, LCD_ROWS - 1);
    lcd_putc('[');
    for (int i = 0; i < bar_w; i++) lcd_putc(i < filled ? '#' : ' ');
    lcd_putc(']');

    _lcd_unlock();
}

/* ============================================================
 * Graphics primitives
 * ============================================================ */

void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color) {
    LCD_ROUTE_VOID(LCD_OP_FILL_RECT,
        .x = x, .y = y, .w = w, .h = h, .color = color);
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT || w == 0 || h == 0) return;
    if (x + w > LCD_WIDTH)  w = LCD_WIDTH - x;
    if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;
    _lcd_lock();

    uint8_t r = (uint8_t)((color >> 16) & 0xFC);
    uint8_t g = (uint8_t)((color >> 8) & 0xFC);
    uint8_t b = (uint8_t)(color & 0xFC);

    static uint8_t linebuf[LCD_WIDTH * 3];
    for (int i = 0; i < w; i++) {
        linebuf[i * 3 + 0] = r;
        linebuf[i * 3 + 1] = g;
        linebuf[i * 3 + 2] = b;
    }

    _set_window(x, y, (uint16_t)(x + w - 1), (uint16_t)(y + h - 1));
    size_t row_bytes = (size_t)w * 3;
    for (int row = 0; row < h; row++) {
        _spi_write_fast(linebuf, row_bytes);
    }
    _end_window_write();
    _lcd_unlock();
}

/* Write one scanline of 3-byte-per-pixel data at (x, y). The `.mapp` graphics
   layer expands a dirty row with gfx_row_to_rgb888() and calls this; the window
   is set per row so a full-surface present never needs a full-frame buffer. */
void lcd_blit_row(uint16_t x, uint16_t y, uint16_t w, const uint8_t *rgb888) {
    LCD_ROUTE_VOID(LCD_OP_BLIT_ROW, .x = x, .y = y, .w = w, .pixels = rgb888);
    if (!rgb888 || x >= LCD_WIDTH || y >= LCD_HEIGHT || w == 0) return;
    if (x + w > LCD_WIDTH) w = (uint16_t)(LCD_WIDTH - x);
    _lcd_lock();
    _set_window(x, y, (uint16_t)(x + w - 1), y);
    _spi_write_fast(rgb888, (size_t)w * 3u);
    _end_window_write();
    _lcd_unlock();
}

void lcd_draw_hline(uint16_t x, uint16_t y, uint16_t w, uint32_t color) {
    LCD_ROUTE_VOID(LCD_OP_DRAW_HLINE, .x = x, .y = y, .w = w, .color = color);
    lcd_fill_rect(x, y, w, 1, color);
}

void lcd_draw_vline(uint16_t x, uint16_t y, uint16_t h, uint32_t color) {
    LCD_ROUTE_VOID(LCD_OP_DRAW_VLINE, .x = x, .y = y, .h = h, .color = color);
    lcd_fill_rect(x, y, 1, h, color);
}

void lcd_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color) {
    LCD_ROUTE_VOID(LCD_OP_DRAW_RECT,
        .x = x, .y = y, .w = w, .h = h, .color = color);
    lcd_draw_hline(x, y, w, color);
    lcd_draw_hline(x, (uint16_t)(y + h - 1), w, color);
    lcd_draw_vline(x, y, h, color);
    lcd_draw_vline((uint16_t)(x + w - 1), y, h, color);
}

/* ============================================================
 * Terminal color and cursor control
 * ============================================================ */

void lcd_set_fg(uint32_t color) {
    LCD_ROUTE_VOID(LCD_OP_SET_FG, .color = color);
    _fg = color;
}

void lcd_set_bg(uint32_t color) {
    LCD_ROUTE_VOID(LCD_OP_SET_BG, .color = color);
    _bg = color;
}

uint32_t lcd_get_fg(void) {
    LCD_ROUTE_VALUE(LCD_OP_GET_FG, result_color);
    return _fg;
}

uint32_t lcd_get_bg(void) {
    LCD_ROUTE_VALUE(LCD_OP_GET_BG, result_color);
    return _bg;
}

void lcd_set_cursor(int col, int row) {
    LCD_ROUTE_VOID(LCD_OP_SET_CURSOR, .i = col, .j = row);
    _lcd_lock();
    if (_cursor_enabled) _lcd_cursor_render(false);
    if (col >= 0 && col < LCD_COLS) _cur_col = col;
    if (row >= 0 && row < LCD_ROWS) _cur_row = row;
    if (_cursor_enabled) _lcd_cursor_render(true);
    _lcd_unlock();
}

int lcd_get_col(void) {
    LCD_ROUTE_VALUE(LCD_OP_GET_COL, result_int);
    return _cur_col;
}

int lcd_get_row(void) {
    LCD_ROUTE_VALUE(LCD_OP_GET_ROW, result_int);
    return _cur_row;
}

void lcd_draw_cell(int col, int row, char c, uint32_t fg, uint32_t bg) {
    LCD_ROUTE_VOID(LCD_OP_DRAW_CELL,
        .i = col, .j = row, .ch = c, .fg = fg, .bg = bg);
    if (col < 0 || col >= LCD_COLS || row < 0 || row >= LCD_ROWS) return;
    if ((uint8_t)c < 0x20 || (uint8_t)c > 0x7E) c = ' ';
    _lcd_lock();
    _text_buf[row][col]  = c;
    _color_buf[row][col] = fg;
    lcd_draw_char((uint16_t)(col * LCD_CHAR_W), (uint16_t)(row * LCD_CHAR_H), c, fg, bg);
    _lcd_unlock();
}

char lcd_get_cell(int col, int row) {
    LCD_ROUTE_VALUE(LCD_OP_GET_CELL, result_char, .i = col, .j = row);
    if (col < 0 || col >= LCD_COLS || row < 0 || row >= LCD_ROWS) return ' ';
    return _text_buf[row][col];
}

/* Read-only access to the built-in 8x16 font, for the .mapp graphics layer
   (which draws glyphs into its own indexed surface). Returns NULL outside
   the printable ASCII range. */
const uint8_t *lcd_font_glyph(char ch) {
    unsigned char c = (unsigned char)ch;
    if (c < 0x20 || c > 0x7E) return NULL;
    return _font8x16[c - 0x20];
}
