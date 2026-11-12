/* kbd.c -- PS/2 keyboard: scancodes (set 1, US layout) -> ASCII.
 *
 * Each key press raises IRQ 1; we must read the scancode from port 0x60,
 * or the controller sends no more interrupts. Bit 7 set = key released.
 */
#include <stdint.h>
#include "dev/kbd.h"
#include "dev/console.h"
#include "arch/i386/pic.h"
#include "arch/i386/trap.h"
#include "arch/i386/io.h"

#define KBD_DATA 0x60

#define SC_RELEASED 0x80
#define SC_LSHIFT   0x2A
#define SC_RSHIFT   0x36
#define SC_CTRL     0x1D
#define SC_CAPSLOCK 0x3A

/* Index = scancode. 0 = no character (function keys, etc.) */
static const char normal_map[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t','q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,   '\\','z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0,   ' ',
};

static const char shift_map[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t','Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0,   ' ',
};

static int shift, ctrl, capslock;

/* Returns the ASCII character for a scancode, or 0 if none. */
static char translate(uint8_t sc)
{
    int released = sc & SC_RELEASED;
    sc &= ~SC_RELEASED;

    switch (sc) {
    case SC_LSHIFT:
    case SC_RSHIFT:   shift = !released; return 0;
    case SC_CTRL:     ctrl = !released;  return 0;
    case SC_CAPSLOCK: if (!released) capslock = !capslock; return 0;
    }
    if (released)
        return 0;

    char c = shift ? shift_map[sc] : normal_map[sc];
    if (capslock && c >= 'a' && c <= 'z')
        c -= 'a' - 'A';
    else if (capslock && c >= 'A' && c <= 'Z')
        c += 'a' - 'A';
    if (ctrl && c >= 'a' && c <= 'z')
        c -= 'a' - 1;                   /* Ctrl-A = 1 ... Ctrl-Z = 26 */
    return c;
}

static void kbd_irq(void)
{
    char c = translate(inb(KBD_DATA));
    if (c)
        console_putc(c);                /* lesson 18: hand it to the TTY instead */
}

void kbd_init(void)
{
    irq_register(IRQ_KEYBOARD, kbd_irq);
    pic_enable(IRQ_KEYBOARD);
}
