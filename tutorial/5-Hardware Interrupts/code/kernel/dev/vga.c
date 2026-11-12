/* vga.c -- VGA text mode: 80x25 cells of (char | color << 8) at 0xB8000. */
#include <stdint.h>
#include "dev/vga.h"
#include "arch/i386/io.h"

#define VGA_MEM   ((volatile uint16_t *)0xB8000)
#define COLS      80
#define ROWS      25
#define COLOR     0x07              /* light grey on black */

/* CRT controller ports, used to move the blinking cursor */
#define CRTC_INDEX 0x3D4
#define CRTC_DATA  0x3D5

static int row, col;

static void put_cell(int r, int c, char ch)
{
    VGA_MEM[r * COLS + c] = (uint16_t)(COLOR << 8) | (uint8_t)ch;
}

static void move_cursor(void)
{
    uint16_t pos = (uint16_t)(row * COLS + col);
    outb(CRTC_INDEX, 0x0F);
    outb(CRTC_DATA, pos & 0xFF);
    outb(CRTC_INDEX, 0x0E);
    outb(CRTC_DATA, pos >> 8);
}

/* Move every line up by one and blank the last line. */
static void scroll(void)
{
    for (int i = 0; i < (ROWS - 1) * COLS; i++)
        VGA_MEM[i] = VGA_MEM[i + COLS];
    for (int c = 0; c < COLS; c++)
        put_cell(ROWS - 1, c, ' ');
    row = ROWS - 1;
}

void vga_init(void)
{
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            put_cell(r, c, ' ');
    row = col = 0;
    move_cursor();
}

void vga_putc(char ch)
{
    switch (ch) {
    case '\n': row++; col = 0;              break;
    case '\r': col = 0;                     break;
    case '\b': if (col > 0) col--;          break;
    case '\t': col = (col + 8) & ~7;        break;
    default:   put_cell(row, col, ch); col++; break;
    }
    if (col >= COLS) {
        col = 0;
        row++;
    }
    if (row >= ROWS)
        scroll();
    move_cursor();
}
