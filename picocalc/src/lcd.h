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
