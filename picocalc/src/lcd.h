#pragma once
/*
 * lcd.h — Minimal ILI9488 text + pixel driver for Mellivora PicoCalc port.
 *
 * The ILI9488 on PicoCalc uses SPI1 at 25 MHz.
 * 320 × 320 resolution, 24-bit BGR colour over 3-byte pixel writes.
 *
 * Public API
 * ----------
 *   lcd_init()                   — hardware reset + command sequence
 *   lcd_fill(color)              — fill screen with one colour
 *   lcd_draw_pixel(x,y,color)    — single pixel
 *   lcd_draw_char(x,y,c,fg,bg)  — 8×16 character cell
 *   lcd_draw_str(x,y,s,fg,bg)   — null-terminated string
 *   lcd_cls(bg)                  — clear screen, reset cursor
 *   lcd_putc(c)                  — print character, advance cursor (wraps/scrolls)
 *   lcd_puts(s)                  — print null-terminated string via lcd_putc
 */
#include <stdint.h>
#include <stddef.h>

#include "picocalc_hw.h"

/* 24-bit colour helpers (R G B bytes) */
#define LCD_RGB(r,g,b)  ((uint32_t)(((uint32_t)(r)<<16)|((uint32_t)(g)<<8)|(b)))
#define LCD_BLACK   LCD_RGB(0,0,0)
#define LCD_WHITE   LCD_RGB(255,255,255)
#define LCD_GREEN   LCD_RGB(0,200,0)
#define LCD_CYAN    LCD_RGB(0,200,200)
#define LCD_YELLOW  LCD_RGB(255,255,0)
#define LCD_RED     LCD_RGB(200,0,0)
#define LCD_BLUE    LCD_RGB(0,0,200)
#define LCD_ORANGE  LCD_RGB(255,165,0)
#define LCD_MAGENTA LCD_RGB(200,0,200)
#define LCD_GREY    LCD_RGB(128,128,128)
#define LCD_DKGREY  LCD_RGB(64,64,64)
#define LCD_AMBER   LCD_RGB(255,176,0)

/* Character cell size (using built-in 8×16 bitmap font) */
#define LCD_CHAR_W  8
#define LCD_CHAR_H  16
#define LCD_COLS    (LCD_WIDTH  / LCD_CHAR_W)   /* 40 */
#define LCD_ROWS    (LCD_HEIGHT / LCD_CHAR_H)   /* 20 */

void lcd_init(void);
void lcd_fill(uint32_t color);
void lcd_draw_pixel(uint16_t x, uint16_t y, uint32_t color);
void lcd_draw_char(uint16_t x, uint16_t y, char c, uint32_t fg, uint32_t bg);
void lcd_draw_str(uint16_t x, uint16_t y, const char *s, uint32_t fg, uint32_t bg);
void lcd_cls(uint32_t bg);
void lcd_putc(char c);
void lcd_puts(const char *s);

/* Graphics primitives */
void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color);
void lcd_draw_hline(uint16_t x, uint16_t y, uint16_t w, uint32_t color);
void lcd_draw_vline(uint16_t x, uint16_t y, uint16_t h, uint32_t color);
void lcd_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color);

/* Terminal color control */
void lcd_set_fg(uint32_t color);
void lcd_set_bg(uint32_t color);
uint32_t lcd_get_fg(void);
uint32_t lcd_get_bg(void);

/* Cursor position */
void lcd_set_cursor(int col, int row);
int  lcd_get_col(void);
int  lcd_get_row(void);

/* Draw a character at cell position with specific colors (no cursor advance) */
void lcd_draw_cell(int col, int row, char c, uint32_t fg, uint32_t bg);

/* Read a single text-buffer cell (returns ' ' for out-of-range). */
char lcd_get_cell(int col, int row);

/* Cross-core serialisation. Recursive: nested calls from the same core are
   safe. Use to make multi-call drawing sequences atomic versus the core1
   status-bar refresh on RP2350. */
void lcd_lock(void);
void lcd_unlock(void);
