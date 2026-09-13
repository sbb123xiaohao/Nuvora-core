#include "kernel.h"
#include <stdarg.h>
static volatile u16 *const vga = (u16 *)0xb8000;
static u32 row, col;
static bool serial_ok;
static u32 input[256];
static u32 screen_owner;
static u16 saved_screen[2000], last_screen[2000];
static u32 saved_row, saved_col;
static bool last_valid;
static u32 input_head, input_tail;
static bool lshift, rshift, lctrl, rctrl, lalt, ralt, caps, extended;
static u32 pause_bytes;
static void cursor(void) {
    u16 p = (u16)(row * 80 + col);
    outb(0x3d4, 14);
    outb(0x3d5, (u8)(p >> 8));
    outb(0x3d4, 15);
    outb(0x3d5, (u8)p);
}
void console_clear(void) {
    for (u32 i = 0; i < 80 * 25; ++i)
        vga[i] = 0x0720;
    row = col = 0;
    cursor();
}
void console_init(void) {
    outb(0x3f9, 0);
    outb(0x3fb, 0x80);
    outb(0x3f8, 1);
    outb(0x3f9, 0);
    outb(0x3fb, 3);
    outb(0x3fa, 0xc7);
    outb(0x3fc, 0x0b);
    serial_ok = inb(0x3fd) != 0xff;
    console_clear();
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
            vga[row * 80 + col] = 0x0720;
        }
    } else if (c == '\t') {
        do {
            console_putc(' ');
        } while (col % 4);
        return;
    } else if ((u8)c >= 32 && (u8)c < 127) {
        serial_putc(c);
        vga[row * 80 + col] = (u16)(0x0700 | (u8)c);
        if (++col == 80) {
            col = 0;
            ++row;
        }
    }
    if (row >= 25) {
        for (u32 i = 0; i < 24 * 80; ++i)
            vga[i] = vga[i + 80];
        for (u32 i = 24 * 80; i < 25 * 80; ++i)
            vga[i] = 0x0720;
        row = 24;
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
        vga[i] = saved_screen[i];
    row = saved_row;
    col = saved_col;
    outb(0x3d4, 10);
    outb(0x3d5, 14);
    cursor();
    serial_text("\033[0m\033[?25h\033[?7h\033[?1049l");
}
int console_surface(u32 pid, u32 op, const struct nv_surface *screen) {
    if (op == NV_SCREEN_ACQUIRE) {
        if (screen_owner && screen_owner != pid)
            return -NV_EBUSY;
        if (screen_owner == pid)
            return 0;
        saved_row = row;
        saved_col = col;
        for (u32 i = 0; i < 2000; ++i)
            saved_screen[i] = vga[i];
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
    if (screen->cursor > 2000)
        return -NV_EINVAL;
    /* Restrict characters to the terminal's supported ASCII repertoire; bit 7
       of the attribute (hardware blink) is intentionally not exposed. */
    for (u32 i = 0; i < 2000; ++i) {
        u8 c = screen->cells[i] & 255;
        if (c < 32 || c > 126 || (screen->cells[i] & 0x8000))
            return -NV_EINVAL;
    }
    static const u8 colors[8] = {0, 4, 2, 6, 1, 5, 3, 7};
    for (u32 y = 0; y < 25; ++y) {
        bool changed = !last_valid || memcmp(last_screen + y * 80, screen->cells + y * 80, 160);
        if (!changed)
            continue;
        ansi_position(y, 0);
        u32 previous = 0xffffffff;
        for (u32 x = 0; x < 80; ++x) {
            u32 i = y * 80 + x;
            u16 cell = screen->cells[i];
            vga[i] = last_screen[i] = cell;
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
    if (screen->cursor == 2000) {
        outb(0x3d4, 10);
        outb(0x3d5, 32);
        serial_text("\033[?25l");
    } else {
        row = screen->cursor / 80;
        col = screen->cursor % 80;
        outb(0x3d4, 10);
        outb(0x3d5, 14);
        cursor();
        ansi_position(row, col);
        serial_text("\033[?25h");
    }
    return 0;
}
