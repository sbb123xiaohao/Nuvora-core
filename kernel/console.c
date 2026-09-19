#include "kernel.h"
#include <stdarg.h>
/* One cell buffer is the source of truth for every output path. The BIOS path
 * mirrors it into VGA text memory; the UEFI path renders it into the GOP
 * linear framebuffer through the FB_WINDOW aperture. Serial always mirrors. */
static volatile u16 *const vga = (u16 *)0xb8000;
static u16 screen[80 * 25];
static u32 row, col;
static bool serial_ok;
static bool vga_present = true;
static bool fb_mode, fb_ready;
static u32 fb_width, fb_height, fb_pitch, fb_format;
static u32 fb_cellw, fb_cellh, fb_offx, fb_offy, fb_scale;
static u32 fb_cursor_cell = 80 * 25; /* index holding the drawn cursor */
static u32 input[256];
static u32 screen_owner;
static u16 saved_screen[2000], last_screen[2000];
static u32 saved_row, saved_col;
static bool last_valid;
static u32 input_head, input_tail;
static bool lshift, rshift, lctrl, rctrl, lalt, ralt, caps, extended;
static u32 pause_bytes;
/* 5x7 glyphs for ASCII 32..126. Bit 4 of each row byte is the left pixel. */
static const u8 font[95][7] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x04, 0x04, 0x04, 0x04, 0x00, 0x00, 0x04},
    {0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A},
    {0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04}, {0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13},
    {0x0C, 0x12, 0x11, 0x08, 0x15, 0x12, 0x0D}, {0x04, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00},
    {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}, {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08},
    {0x04, 0x15, 0x0E, 0x1F, 0x0E, 0x15, 0x04}, {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08}, {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x04}, {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10},
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
    {0x00, 0x04, 0x04, 0x00, 0x00, 0x04, 0x04}, {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x08},
    {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}, {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00},
    {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}, {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04},
    {0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0E}, {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},
    {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}, {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F},
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F},
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},
    {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}, {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11},
    {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}, {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D},
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E},
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11},
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, {0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04},
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}, {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E},
    {0x10, 0x10, 0x08, 0x04, 0x02, 0x01, 0x01}, {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E},
    {0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F},
    {0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F},
    {0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x1E}, {0x00, 0x00, 0x0F, 0x10, 0x10, 0x10, 0x0F},
    {0x01, 0x01, 0x0F, 0x11, 0x11, 0x11, 0x0F}, {0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E},
    {0x06, 0x08, 0x1E, 0x08, 0x08, 0x08, 0x08}, {0x00, 0x00, 0x0F, 0x11, 0x11, 0x0F, 0x01},
    {0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x11}, {0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E},
    {0x02, 0x00, 0x02, 0x02, 0x02, 0x12, 0x0C}, {0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12},
    {0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, {0x00, 0x00, 0x1A, 0x15, 0x15, 0x15, 0x15},
    {0x00, 0x00, 0x1E, 0x11, 0x11, 0x11, 0x11}, {0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E},
    {0x00, 0x00, 0x1E, 0x11, 0x11, 0x1E, 0x10}, {0x00, 0x00, 0x0F, 0x11, 0x11, 0x0F, 0x01},
    {0x00, 0x00, 0x1E, 0x10, 0x10, 0x10, 0x10}, {0x00, 0x00, 0x0F, 0x10, 0x0E, 0x01, 0x1E},
    {0x08, 0x08, 0x1E, 0x08, 0x08, 0x09, 0x06}, {0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D},
    {0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04}, {0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0A},
    {0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11}, {0x00, 0x00, 0x11, 0x11, 0x11, 0x0F, 0x01},
    {0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F}, {0x06, 0x08, 0x08, 0x18, 0x08, 0x08, 0x06},
    {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, {0x0C, 0x02, 0x02, 0x03, 0x02, 0x02, 0x0C},
    {0x00, 0x00, 0x08, 0x15, 0x02, 0x00, 0x00}};
static u32 palette(u8 index) {
    static const u8 red[16] = {0, 0, 0, 0, 170, 170, 170, 170, 85, 85, 85, 85, 255, 255, 255, 255};
    static const u8 green[16] = {0, 0, 170, 170, 0, 0, 85, 170, 85, 85, 255, 255, 85, 85, 255, 255};
    static const u8 blue[16] = {0, 170, 0, 170, 0, 170, 0, 170, 85, 255, 85, 255, 85, 255, 85, 255};
    index &= 15;
    u32 r = red[index], g = green[index], b = blue[index];
    return fb_format == NV_FB_RGBX8 ? r | (g << 8) | (b << 16) : b | (g << 8) | (r << 16);
}
static u32 *fb_row(u32 y) {
    return (u32 *)(void *)((u8 *)FB_WINDOW + (uptr)y * fb_pitch);
}
static void fb_cell(u32 index, u16 cell, bool invert) {
    u32 cx = index % 80, cy = index / 80;
    u32 x0 = fb_offx + cx * fb_cellw, y0 = fb_offy + cy * fb_cellh;
    if (x0 + fb_cellw > fb_width || y0 + fb_cellh > fb_height)
        return;
    u32 attr = cell >> 8;
    u32 fg = palette((attr & 7) | (attr & 8)), bg = palette((attr >> 4) & 7);
    if (invert) {
        u32 t = fg;
        fg = bg;
        bg = t;
    }
    for (u32 y = y0; y < y0 + fb_cellh; ++y) {
        u32 *line = fb_row(y);
        for (u32 x = x0; x < x0 + fb_cellw; ++x)
            line[x] = bg;
    }
    if ((cell & 255) < 32 || (cell & 255) > 126)
        return;
    const u8 *glyph = font[(cell & 255) - 32];
    u32 gx = x0 + (fb_cellw - 5 * fb_scale) / 2, gy = y0 + (fb_cellh - 7 * fb_scale) / 2;
    for (u32 r = 0; r < 7; ++r) {
        u8 bits = glyph[r];
        if (!bits)
            continue;
        for (u32 c = 0; c < 5; ++c) {
            if (!(bits & (0x10 >> c)))
                continue;
            for (u32 sy = 0; sy < fb_scale; ++sy) {
                u32 *line = fb_row(gy + r * fb_scale + sy);
                for (u32 sx = 0; sx < fb_scale; ++sx)
                    line[gx + c * fb_scale + sx] = fg;
            }
        }
    }
}
static void fb_cursor_erase(void) {
    if (fb_cursor_cell < 80 * 25) {
        fb_cell(fb_cursor_cell, screen[fb_cursor_cell], false);
        fb_cursor_cell = 80 * 25;
    }
}
static void fb_cursor_draw(void) {
    u32 index = row * 80 + col;
    if (index >= 80 * 25)
        index = 80 * 25 - 1;
    fb_cell(index, screen[index], true);
    fb_cursor_cell = index;
}
static void fb_scroll(void) {
    for (u32 i = 0; i < 80 * 25; ++i)
        fb_cell(i, screen[i], false);
}
static void fb_render_all(void) {
    fb_scroll();
    fb_cursor_draw();
}
static void cursor(void) {
    if (fb_mode) {
        if (fb_ready) {
            fb_cursor_erase();
            fb_cursor_draw();
        }
        return;
    }
    u16 p = (u16)(row * 80 + col);
    outb(0x3d4, 14);
    outb(0x3d5, (u8)(p >> 8));
    outb(0x3d4, 15);
    outb(0x3d5, (u8)p);
}
void console_fb_enable(void) {
    if (fb_mode && fb_window_mapped) {
        fb_ready = true;
        fb_render_all();
    }
}
void console_clear(void) {
    for (u32 i = 0; i < 80 * 25; ++i)
        screen[i] = 0x0720;
    if (!fb_mode) {
        for (u32 i = 0; i < 80 * 25; ++i)
            vga[i] = 0x0720;
    }
    row = col = 0;
    if (!fb_mode || fb_ready)
        cursor();
}
void console_init(const struct boot_info *bi) {
    outb(0x3f9, 0);
    outb(0x3fb, 0x80);
    outb(0x3f8, 1);
    outb(0x3f9, 0);
    outb(0x3fb, 3);
    outb(0x3fa, 0xc7);
    outb(0x3fc, 0x0b);
    serial_ok = inb(0x3fd) != 0xff;
    if (bi && bi->fb.format != NV_FB_NONE && bi->fb.width >= 80 && bi->fb.height >= 25 &&
        bi->fb.pitch >= bi->fb.width * 4) {
        fb_mode = true;
        vga_present = false;
        fb_width = bi->fb.width;
        fb_height = bi->fb.height;
        fb_pitch = bi->fb.pitch;
        fb_format = bi->fb.format;
        fb_cellw = fb_width / 80;
        fb_cellh = fb_height / 25;
        fb_offx = (fb_width - 80 * fb_cellw) / 2;
        fb_offy = (fb_height - 25 * fb_cellh) / 2;
        fb_scale = MAX(1u, MIN(fb_cellw / 6, fb_cellh / 8));
    }
    console_clear();
}
static void vga_cell(u32 index, u16 cell) {
    if (vga_present)
        vga[index] = cell;
}
static void vga_scroll(void) {
    if (!vga_present)
        return;
    for (u32 i = 0; i < 24 * 80; ++i)
        vga[i] = vga[i + 80];
    for (u32 i = 24 * 80; i < 25 * 80; ++i)
        vga[i] = 0x0720;
}
static void screen_scroll(void) {
    for (u32 i = 0; i < 24 * 80; ++i)
        screen[i] = screen[i + 80];
    for (u32 i = 24 * 80; i < 25 * 80; ++i)
        screen[i] = 0x0720;
    row = 24;
}
static void serial_putc(char c) {
    if (!serial_ok)
        return;
    u32 timeout = 100000;
    while (!(inb(0x3fd) & 0x20) && --timeout) {
    }
    if (timeout)
        outb(0x3f8, (u8)c);
}
void console_putc(char c) {
    if (screen_owner) {
        if (c == '\n')
            serial_putc('\r');
        serial_putc(c);
        last_valid = false;
        return;
    }
    if (c == '\n') {
        serial_putc('\r');
        serial_putc('\n');
        col = 0;
        ++row;
    } else if (c == '\r') {
        serial_putc(c);
        col = 0;
    } else if (c == '\b') {
        serial_putc(c);
        if (col) {
            --col;
            screen[row * 80 + col] = 0x0720;
            vga_cell(row * 80 + col, 0x0720);
            if (fb_ready)
                fb_cell(row * 80 + col, 0x0720, false);
        }
    } else if (c == '\t') {
        do {
            console_putc(' ');
        } while (col % 4);
        return;
    } else if ((u8)c >= 32 && (u8)c < 127) {
        serial_putc(c);
        u32 index = row * 80 + col;
        screen[index] = (u16)(0x0700 | (u8)c);
        vga_cell(index, screen[index]);
        if (fb_ready)
            fb_cell(index, screen[index], false);
        if (++col == 80) {
            col = 0;
            ++row;
        }
    }
    if (row >= 25) {
        screen_scroll();
        vga_scroll();
        if (fb_ready)
            fb_scroll();
    }
    cursor();
}
void console_write(const char *s, usize n) {
    while (n--)
        console_putc(*s++);
}
void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    while (*fmt) {
        if (*fmt != '%') {
            console_putc(*fmt++);
            continue;
        }
        ++fmt;
        char b[34];
        usize n = 0;
        switch (*fmt++) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            console_write(s, strlen(s));
            break;
        }
        case 'u':
            n = number(b, va_arg(ap, u32), 10);
            console_write(b, n);
            break;
        case 'x':
            n = number(b, va_arg(ap, u32), 16);
            console_write(b, n);
            break;
        case 'd': {
            i32 v = va_arg(ap, i32);
            if (v < 0)
                console_putc('-');
            n = number(b, v < 0 ? 0u - (u32)v : (u32)v, 10);
            console_write(b, n);
            break;
        }
        case 'c':
            console_putc((char)va_arg(ap, int));
            break;
        case '%':
            console_putc('%');
            break;
        default:
            console_putc('?');
            break;
        }
    }
    va_end(ap);
}
NORETURN void panic(const char *s) {
    irq_disable();
    console_release(screen_owner);
    kprintf("\nKERNEL PANIC: %s\n", s);
    if (test_mode)
        outl(0xf4, 0x11);
    for (;;)
        __asm__ volatile("hlt");
}
static const char keys[128] = {
    [1] = 27,    [2] = '1',  [3] = '2',  [4] = '3',   [5] = '4',  [6] = '5',   [7] = '6',
    [8] = '7',   [9] = '8',  [10] = '9', [11] = '0',  [12] = '-', [13] = '=',  [14] = '\b',
    [15] = '\t', [16] = 'q', [17] = 'w', [18] = 'e',  [19] = 'r', [20] = 't',  [21] = 'y',
    [22] = 'u',  [23] = 'i', [24] = 'o', [25] = 'p',  [26] = '[', [27] = ']',  [28] = '\n',
    [30] = 'a',  [31] = 's', [32] = 'd', [33] = 'f',  [34] = 'g', [35] = 'h',  [36] = 'j',
    [37] = 'k',  [38] = 'l', [39] = ';', [40] = '\'', [41] = '`', [43] = '\\', [44] = 'z',
    [45] = 'x',  [46] = 'c', [47] = 'v', [48] = 'b',  [49] = 'n', [50] = 'm',  [51] = ',',
    [52] = '.',  [53] = '/', [57] = ' '};
static void queue_key(u32 c) {
    u32 next = (input_head + 1) % ARRAY_LEN(input);
    if (c && next != input_tail) {
        input[input_head] = c;
        input_head = next;
    }
}
void keyboard_irq(void) {
    u8 s = inb(0x60);
    if (pause_bytes) {
        --pause_bytes;
        return;
    }
    if (s == 0xe1) {
        pause_bytes = 5;
        return;
    }
    if (s == 0xe0) {
        extended = true;
        return;
    }
    bool ext = extended;
    extended = false;
    u8 code = s & 127;
    bool down = !(s & 128);
    if (code == 29) {
        if (ext)
            rctrl = down;
        else
            lctrl = down;
        return;
    }
    if (code == 56) {
        if (ext)
            ralt = down;
        else
            lalt = down;
        return;
    }
    if (!ext && code == 42) {
        lshift = down;
        return;
    }
    if (!ext && code == 54) {
        rshift = down;
        return;
    }
    if (!down)
        return;
    if (!ext && code == 58) {
        caps = !caps;
        return;
    }
    u32 c = 0;
    bool shifted = lshift || rshift, ctrl = lctrl || rctrl;
    if (ext || (code >= 71 && code <= 83)) {
        switch (code) {
        case 75:
            c = NV_KEY_LEFT;
            break;
        case 77:
            c = NV_KEY_RIGHT;
            break;
        case 72:
            c = NV_KEY_UP;
            break;
        case 80:
            c = NV_KEY_DOWN;
            break;
        case 71:
            c = NV_KEY_HOME;
            break;
        case 79:
            c = NV_KEY_END;
            break;
        case 73:
            c = NV_KEY_PGUP;
            break;
        case 81:
            c = NV_KEY_PGDN;
            break;
        case 83:
            c = NV_KEY_DELETE;
            break;
        case 82:
            c = NV_KEY_INSERT;
            break;
        case 28:
            c = '\n';
            break;
        case 53:
            c = '/';
            break;
        }
    } else if (code >= 59 && code <= 68)
        c = NV_KEY_F1 + code - 59;
    else if (code == 87 || code == 88)
        c = NV_KEY_F11 + code - 87;
    else {
        c = (u8)keys[code];
        if (c >= 'a' && c <= 'z') {
            if (!ctrl && shifted != caps)
                c = c - 'a' + 'A';
        } else if (shifted) {
            const char *a = "1234567890-=[];'`,./\\", *b = "!@#$%^&*()_+{}:\"~<>?|";
            for (u32 i = 0; a[i]; ++i)
                if (c == (u8)a[i]) {
                    c = (u8)b[i];
                    break;
                }
        }
    }
    if (c)
        queue_key(c | NV_KEY_DIRECT | (shifted ? NV_KEY_SHIFT : 0) | (ctrl ? NV_KEY_CTRL : 0) |
                  ((lalt || ralt) ? NV_KEY_ALT : 0));
}
void console_usb_key(u8 usage, u8 modifiers) {
    bool shifted = (modifiers & 0x22) != 0, ctrl = (modifiers & 0x11) != 0;
    u32 c = 0;
    if (usage == 0x39) {
        caps = !caps;
        return;
    }
    if (usage >= 4 && usage <= 29) {
        c = 'a' + usage - 4;
        if (!ctrl && shifted != caps)
            c = c - 'a' + 'A';
    } else if (usage >= 30 && usage <= 39) {
        c = (u8)(shifted ? "!@#$%^&*()" : "1234567890")[usage - 30];
    } else if (usage >= 0x3a && usage <= 0x45) {
        c = NV_KEY_F1 + usage - 0x3a;
    } else if (usage >= 0x2d && usage <= 0x38) {
        c = (u8)(shifted ? "_+{}|~:\"~<>?" : "-=[]\\#;'`,./")[usage - 0x2d];
    } else {
        switch (usage) {
        case 0x28: case 0x58: c = '\n'; break;
        case 0x29: c = 27; break;
        case 0x2a: c = '\b'; break;
        case 0x2b: c = '\t'; break;
        case 0x2c: c = ' '; break;
        case 0x49: c = NV_KEY_INSERT; break;
        case 0x4a: c = NV_KEY_HOME; break;
        case 0x4b: c = NV_KEY_PGUP; break;
        case 0x4c: c = NV_KEY_DELETE; break;
        case 0x4d: c = NV_KEY_END; break;
        case 0x4e: c = NV_KEY_PGDN; break;
        case 0x4f: c = NV_KEY_RIGHT; break;
        case 0x50: c = NV_KEY_LEFT; break;
        case 0x51: c = NV_KEY_DOWN; break;
        case 0x52: c = NV_KEY_UP; break;
        case 0x54: c = '/'; break;
        case 0x55: c = '*'; break;
        case 0x56: c = '-'; break;
        case 0x57: c = '+'; break;
        case 0x64: c = shifted ? '|' : '\\'; break;
        }
    }
    if (c)
        queue_key(c | NV_KEY_DIRECT | (shifted ? NV_KEY_SHIFT : 0) | (ctrl ? NV_KEY_CTRL : 0) |
                  ((modifiers & 0x44) ? NV_KEY_ALT : 0));
}
static int raw_key(void) {
    if (input_head != input_tail) {
        u32 c = input[input_tail];
        input_tail = (input_tail + 1) % ARRAY_LEN(input);
        return (int)c;
    }
    if (serial_ok && (inb(0x3fd) & 1)) {
        u8 c = inb(0x3f8);
        return c == '\r' ? '\n' : c;
    }
    return -NV_EAGAIN;
}
int console_getc(void) {
    if (screen_owner)
        return -1;
    for (u32 i = 0; i < ARRAY_LEN(input); ++i) {
        int r = raw_key();
        if (r < 0)
            return -1;
        u32 c = (u32)r & 4095;
        if (c >= 256 || ((u32)r & NV_KEY_ALT))
            continue;
        if (((u32)r & NV_KEY_CTRL) && c >= 'a' && c <= 'z')
            c -= 'a' - 1;
        return (int)c;
    }
    return -1;
}
int console_key(u32 pid) {
    return screen_owner == pid ? raw_key() : -NV_EACCESS;
}
bool console_owned(u32 pid) {
    return screen_owner == pid;
}
static void serial_text(const char *s) {
    while (*s)
        serial_putc(*s++);
}
static void serial_number(u32 n) {
    char b[32];
    number(b, n, 10);
    serial_text(b);
}
static void ansi_position(u32 r, u32 c) {
    serial_text("\033[");
    serial_number(r + 1);
    serial_putc(';');
    serial_number(c + 1);
    serial_putc('H');
}
void console_release(u32 pid) {
    if (!screen_owner || screen_owner != pid)
        return;
    screen_owner = 0;
    for (u32 i = 0; i < 2000; ++i)
        screen[i] = saved_screen[i];
    row = saved_row;
    col = saved_col;
    if (fb_mode) {
        if (fb_ready)
            fb_render_all();
    } else {
        for (u32 i = 0; i < 2000; ++i)
            vga[i] = screen[i];
        outb(0x3d4, 10);
        outb(0x3d5, 14);
    }
    cursor();
    serial_text("\033[0m\033[?25h\033[?7h\033[?1049l");
}
int console_surface(u32 pid, u32 op, const struct nv_surface *screen_ptr) {
    if (op == NV_SCREEN_ACQUIRE) {
        if (screen_owner && screen_owner != pid)
            return -NV_EBUSY;
        if (screen_owner == pid)
            return 0;
        saved_row = row;
        saved_col = col;
        for (u32 i = 0; i < 2000; ++i)
            saved_screen[i] = screen[i];
        screen_owner = pid;
        last_valid = false;
        serial_text("\033[?1049h\033[?7l\033[2J");
        return 0;
    }
    if (op != NV_SCREEN_PRESENT && op != NV_SCREEN_RELEASE)
        return -NV_EINVAL;
    if (screen_owner != pid)
        return -NV_EACCESS;
    if (op == NV_SCREEN_RELEASE) {
        console_release(pid);
        return 0;
    }
    if (screen_ptr->cursor > 2000)
        return -NV_EINVAL;
    /* Restrict characters to the terminal's supported ASCII repertoire; bit 7
       of the attribute (hardware blink) is intentionally not exposed. */
    for (u32 i = 0; i < 2000; ++i) {
        u8 c = screen_ptr->cells[i] & 255;
        if (c < 32 || c > 126 || (screen_ptr->cells[i] & 0x8000))
            return -NV_EINVAL;
    }
    static const u8 colors[8] = {0, 4, 2, 6, 1, 5, 3, 7};
    for (u32 y = 0; y < 25; ++y) {
        bool changed = !last_valid || memcmp(last_screen + y * 80, screen_ptr->cells + y * 80, 160);
        if (changed && fb_ready)
            for (u32 x = 0; x < 80; ++x)
                fb_cell(y * 80 + x, screen_ptr->cells[y * 80 + x], false);
        if (!changed)
            continue;
        ansi_position(y, 0);
        u32 previous = 0xffffffff;
        for (u32 x = 0; x < 80; ++x) {
            u32 i = y * 80 + x;
            u16 cell = screen_ptr->cells[i];
            screen[i] = last_screen[i] = cell;
            vga_cell(i, cell);
            u32 a = cell >> 8;
            if (a != previous) {
                serial_text("\033[0;");
                serial_number((a & 8 ? 90 : 30) + colors[a & 7]);
                serial_putc(';');
                serial_number(40 + colors[(a >> 4) & 7]);
                serial_putc('m');
                previous = a;
            }
            serial_putc((char)cell);
        }
    }
    last_valid = true;
    if (screen_ptr->cursor == 2000) {
        if (fb_mode && fb_ready)
            fb_cursor_erase();
        if (!fb_mode) {
            outb(0x3d4, 10);
            outb(0x3d5, 32);
        }
        serial_text("\033[?25l");
    } else {
        row = screen_ptr->cursor / 80;
        col = screen_ptr->cursor % 80;
        if (!fb_mode) {
            outb(0x3d4, 10);
            outb(0x3d5, 14);
        }
        cursor();
        ansi_position(row, col);
        serial_text("\033[?25h");
    }
    return 0;
}
