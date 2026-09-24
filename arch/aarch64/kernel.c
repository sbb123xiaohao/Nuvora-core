#include <nv/string.h>

/* First ARM64 bring-up target: QEMU virt, Image handover, one EL1 CPU.
 * Device addresses and RAM extent come from the FDT, never from board offsets. */
#define PAGE 4096ull
#define RAM_BASE 0x40000000ull
#define RAM_CAP (64ull << 30)
#define BITMAP_BYTES (RAM_CAP / PAGE / 8)
#define FDT_MAX (2u << 20)
#define BLOCK (2ull << 20)
#define GIB (1ull << 30)
#define AF (1ull << 10)
#define SH (3ull << 8)
#define RO (1ull << 7)
#define PXN (1ull << 53)
#define UXN (1ull << 54)
#define KDATA (AF | SH | PXN | UXN)

extern u8 kernel_begin[], user_begin[], user_end[], kernel_text_end[], kernel_ro_end[], kernel_end[];
extern u32 arm64_neon_dot(const u32 *, const u32 *);
extern void arm64_user_start(void), arm64_vectors(void);
static u64 uart, ram_end;
static u32 checks;
static bool user_result, user_isolated;
static u8 used[BITMAP_BYTES];
static u8 owned[BITMAP_BYTES];
static u64 free_count, total_count;
static u64 root[512] ALIGNED(PAGE), low_l2[512] ALIGNED(PAGE);
static u64 image_l3[8][512] ALIGNED(PAGE);
static u64 uart_l2[512] ALIGNED(PAGE), uart_l3[512] ALIGNED(PAGE);
static u64 user_l3[512] ALIGNED(PAGE);
struct region { u64 base, length; };
static struct region ranges[8], reserved[32];
static u32 range_count, reserved_count;

static void halt(void) {
    for (;;) __asm__ volatile("wfi");
}
static void putc(char c) {
    if (!uart) return;
    if (c == '\n') putc('\r');
    volatile u32 *reg = (volatile u32 *)(uptr)uart;
    while (reg[0x18 / 4] & (1u << 5)) {}
    reg[0] = (u8)c;
}
static void puts(const char *s) {
    for (; *s; ++s) putc(*s);
}
static void hex(u64 value) {
    puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
        putc("0123456789abcdef"[(value >> shift) & 15]);
}
static void decimal(u64 n) {
    if (n >= 10) decimal(n / 10);
    putc('0' + n % 10);
}
static void fail(const char *why) {
    puts("ARM64 FAIL: "); puts(why); putc('\n'); halt();
}
static void verify(bool condition, const char *why) {
    if (!condition) fail(why);
    ++checks;
}
static u32 be32(const u8 *p) {
    return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}
static u64 cells(const u8 *p, u32 count) {
    u64 n = 0;
    for (u32 i = 0; i < count; ++i) n = n << 32 | be32(p + 4 * i);
    return n;
}
static void add_region(struct region *dst, u32 *count, u32 cap, u64 base, u64 length) {
    if (!length) return;
    if (*count == cap || base > ~(u64)0 - length) fail("invalid device tree range");
    dst[(*count)++] = (struct region){base, length};
}
static bool compatible(const u8 *value, u32 n) {
    const char name[] = "arm,pl011";
    for (u32 i = 0; i < n;) {
        u32 len = (u32)strnlen((const char *)value + i, n - i);
        if (len == sizeof(name) - 1 && !memcmp(value + i, name, len)) return true;
        if (len == n - i) break;
        i += len + 1;
    }
    return false;
}
struct node {
    u32 ac, sc, count;
    bool memory, serial, reserved;
    struct region reg[4];
};
static void parse_fdt(uptr addr, u32 *dtb_size) {
    if (!addr || (addr & 7)) fail("DTB pointer");
    const u8 *f = (const u8 *)addr;
    if (be32(f) != 0xd00dfeed) fail("DTB magic");
    u32 size = be32(f + 4), structs = be32(f + 8), strings = be32(f + 12);
    u32 reserve_off = be32(f + 16), strings_len = be32(f + 32), structs_len = be32(f + 36);
    if (size < 40 || size > FDT_MAX || structs > size || structs_len > size - structs ||
        strings > size || strings_len > size - strings || reserve_off > size - 16 ||
        (structs & 3) || (reserve_off & 7)) fail("DTB bounds");
    *dtb_size = size;
    for (u32 off = reserve_off; off <= size - 16; off += 16) {
        u64 base = (u64)be32(f + off) << 32 | be32(f + off + 4);
        u64 len = (u64)be32(f + off + 8) << 32 | be32(f + off + 12);
        if (!base && !len) break;
        add_region(reserved, &reserved_count, ARRAY_LEN(reserved), base, len);
        if (off + 32 > size) fail("DTB reservation terminator");
    }
    struct node stack[12] = {0};
    int depth = -1;
    u32 pos = structs, end = structs + structs_len;
    bool done = false;
    while (pos <= end - 4) {
        u32 token = be32(f + pos); pos += 4;
        if (token == 1) {
            if (++depth >= (int)ARRAY_LEN(stack)) fail("DTB nesting");
            u32 len = (u32)strnlen((const char *)f + pos, end - pos);
            if (len == end - pos) fail("DTB node name");
            struct node n = {0};
            n.ac = depth ? stack[depth - 1].ac : 2;
            n.sc = depth ? stack[depth - 1].sc : 1;
            n.memory = depth == 1 && len >= 6 && !memcmp(f + pos, "memory", 6);
            n.reserved = depth && (stack[depth - 1].reserved ||
                         (len == 15 && !memcmp(f + pos, "reserved-memory", 15)));
            stack[depth] = n;
            pos = (pos + len + 4) & ~3u;
            if (pos > end) fail("DTB node overflow");
        } else if (token == 2) {
            if (depth < 0) fail("DTB depth");
            struct node *n = &stack[depth--];
            for (u32 i = 0; i < n->count; ++i) {
                struct region r = n->reg[i];
                if (n->memory)
                    add_region(ranges, &range_count, ARRAY_LEN(ranges), r.base, r.length);
                else if (n->reserved && depth >= 1)
                    add_region(reserved, &reserved_count, ARRAY_LEN(reserved), r.base, r.length);
                else if (n->serial && !uart) uart = r.base;
            }
        } else if (token == 3) {
            if (depth < 0 || pos > end - 8) fail("DTB property");
            u32 len = be32(f + pos), nameoff = be32(f + pos + 4); pos += 8;
            if (len > end - pos || nameoff >= strings_len) fail("DTB property bounds");
            const char *name = (const char *)f + strings + nameoff;
            if (strnlen(name, strings_len - nameoff) == strings_len - nameoff)
                fail("DTB property name");
            struct node *n = &stack[depth];
            if (!strcmp(name, "#address-cells") && len == 4) n->ac = be32(f + pos);
            if (!strcmp(name, "#size-cells") && len == 4) n->sc = be32(f + pos);
            if (!strcmp(name, "device_type") && len >= 7 && !memcmp(f + pos, "memory", 7))
                n->memory = depth == 1;
            if (!strcmp(name, "compatible")) n->serial = compatible(f + pos, len);
            if (!strcmp(name, "reg") && depth && n->ac <= 2 && n->sc <= 2 &&
                n->ac && n->sc && len % (4 * (n->ac + n->sc)) == 0) {
                for (u32 i = 0; i < len; i += 4 * (n->ac + n->sc)) {
                    if (n->count == ARRAY_LEN(n->reg)) fail("DTB reg count");
                    n->reg[n->count++] = (struct region){
                        cells(f + pos + i, n->ac), cells(f + pos + i + 4 * n->ac, n->sc)};
                }
            }
            pos = (pos + len + 3) & ~3u;
            if (pos > end) fail("DTB property overflow");
        } else if (token == 4) {
            continue;
        } else if (token == 9) {
            done = depth == -1;
            break;
        } else fail("DTB token");
    }
    if (!done || !uart || !range_count) fail("DTB RAM or PL011 missing");
}
static void mark(u64 base, u64 length, bool busy) {
    if (!length || base > ~(u64)0 - length) return;
    u64 first = busy ? base / PAGE * PAGE : ALIGN_UP(base, PAGE);
    u64 last = busy ? ALIGN_UP(base + length, PAGE) : (base + length) / PAGE * PAGE;
    if (last <= RAM_BASE || first >= ram_end) return;
    if (first < RAM_BASE) first = RAM_BASE;
    if (last > ram_end) last = ram_end;
    for (u64 pa = first; pa < last; pa += PAGE) {
        u32 index = (u32)((pa - RAM_BASE) / PAGE);
        u8 mask = 1u << (index & 7);
        if (!!(used[index / 8] & mask) != busy) {
            if (busy) { used[index / 8] |= mask; --free_count; }
            else { used[index / 8] &= (u8)~mask; ++free_count; }
        }
    }
}
static void memory_init(uptr dtb, u32 dtb_size) {
    for (u32 i = 0; i < range_count; ++i) {
        u64 end = ranges[i].base + ranges[i].length;
        if (end > ram_end && ranges[i].base < RAM_BASE + RAM_CAP)
            ram_end = MIN(end, RAM_BASE + RAM_CAP);
    }
    if (ram_end < RAM_BASE + (32u << 20) || (uptr)kernel_end >= ram_end)
        fail("insufficient ARM64 RAM");
    memset(used, 0xff, sizeof(used));
    for (u32 i = 0; i < range_count; ++i)
        mark(ranges[i].base, ranges[i].length, false);
    total_count = free_count;
    mark((uptr)kernel_begin, (uptr)kernel_end - (uptr)kernel_begin, true);
    mark(dtb, dtb_size, true);
    for (u32 i = 0; i < reserved_count; ++i)
        mark(reserved[i].base, reserved[i].length, true);
}
static u64 page_run(u32 count) {
    if (!count || count > free_count) return 0;
    u32 run = 0;
    u32 pages = (u32)((ram_end - RAM_BASE) / PAGE);
    for (u32 i = 0; i < pages; ++i) {
        if (used[i / 8] & (1u << (i & 7))) run = 0;
        else if (++run == count) {
            u64 pa = RAM_BASE + ((u64)i + 1 - count) * PAGE;
            mark(pa, (u64)count * PAGE, true);
            for (u32 j = i + 1 - count; j <= i; ++j)
                owned[j / 8] |= 1u << (j & 7);
            memset((void *)(uptr)pa, 0, (usize)count * PAGE);
            return pa;
        }
    }
    return 0;
}
static void page_release(u64 base, u32 count) {
    if (!count || base < RAM_BASE || base % PAGE ||
        base > ram_end || (u64)count * PAGE > ram_end - base) fail("invalid page release");
    for (u32 i = 0; i < count; ++i) {
        u32 index = (u32)((base - RAM_BASE) / PAGE) + i;
        if (!(owned[index / 8] & (1u << (index & 7)))) fail("unowned page release");
    }
    for (u32 i = 0; i < count; ++i) {
        u32 index = (u32)((base - RAM_BASE) / PAGE) + i;
        owned[index / 8] &= (u8)~(1u << (index & 7));
    }
    mark(base, (u64)count * PAGE, false);
}
static void mmu_init(void) {
    u64 pa_bits;
    __asm__ volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(pa_bits));
    if ((pa_bits & 15) < 2 && ram_end > (1ull << 36)) fail("physical address width");
    u64 text = (uptr)kernel_text_end, ro = (uptr)kernel_ro_end;
    for (u64 pa = RAM_BASE; pa < ram_end; pa += GIB)
        root[pa / GIB] = pa | KDATA | 1; /* 1 GiB normal-memory block */
    root[RAM_BASE / GIB] = (uptr)low_l2 | 3;
    for (u32 i = 0; i < 512; ++i)
        low_l2[i] = (RAM_BASE + (u64)i * BLOCK) | KDATA | 1;
    u64 start = (uptr)kernel_begin / BLOCK * BLOCK;
    u32 count = (u32)(ALIGN_UP((uptr)kernel_end, BLOCK) - start) / BLOCK;
    if (count > ARRAY_LEN(image_l3)) fail("kernel page-table cover");
    for (u32 i = 0; i < count; ++i) {
        u64 chunk = start + (u64)i * BLOCK;
        low_l2[(chunk - RAM_BASE) / BLOCK] = (uptr)image_l3[i] | 3;
        for (u32 j = 0; j < 512; ++j) {
            u64 page = chunk + (u64)j * PAGE;
            u64 flags = KDATA;
            if (page >= (uptr)user_begin && page < (uptr)user_end)
                flags = AF | SH | RO | (1u << 6) | PXN; /* EL0 RX, EL1 RO */
            else if (page >= (uptr)kernel_begin && page < text) flags = AF | SH | RO | UXN;
            else if (page >= text && page < ro) flags |= RO;
            image_l3[i][j] = page | flags | 3;
        }
    }
    if (uart >= (1ull << 39) || (uart >= RAM_BASE && uart < ram_end))
        fail("UART outside device window");
    root[uart / GIB] = (uptr)uart_l2 | 3;
    uart_l2[(uart % GIB) / BLOCK] = (uptr)uart_l3 | 3;
    uart_l3[(uart % BLOCK) / PAGE] = uart / PAGE * PAGE | AF | PXN | UXN | (1u << 2) | 3;
    if (uart / BLOCK == 0x20000000ull / BLOCK) fail("user window overlaps UART");
    uart_l2[0x20000000ull / BLOCK] = (uptr)user_l3 | 3;
    u64 tcr = 25 | (1ull << 8) | (1ull << 10) | (3ull << 12) | (2ull << 32);
    __asm__ volatile("dsb sy; msr mair_el1, %0; msr tcr_el1, %1; msr ttbr0_el1, %2;"
                     "isb; tlbi vmalle1; dsb sy; isb"
                     :: "r"(0x04ffull), "r"(tcr), "r"((uptr)root) : "memory");
    u64 control;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(control));
    control |= (1u << 0) | (1u << 2) | (1u << 12); /* MMU, D-cache, I-cache */
    __asm__ volatile("msr sctlr_el1, %0; isb" :: "r"(control) : "memory");
}
void arm64_exception(u64 esr, u64 far, u64 *regs) {
    u32 ec = (u32)(esr >> 26) & 0x3f;
    if (ec == 0x15 && (esr & 0xffff) == 0) {
        u32 *buffer = (u32 *)(uptr)0x20000000;
        verify(regs[0] == (uptr)buffer && regs[1] == 70 && buffer[0] == 1 &&
               buffer[7] == 8, "EL0 NEON compute result");
        user_result = true;
        regs[0] = 0;
        puts("[ok] EL0 NEON workload completed via SVC\n");
        return;
    }
    if (ec == 0x24 && far == (uptr)kernel_begin && user_result && !user_isolated) {
        u64 pc;
        __asm__ volatile("mrs %0, elr_el1" : "=r"(pc));
        pc += 4; /* resume after the expected, single faulting load */
        __asm__ volatile("msr elr_el1, %0; isb" :: "r"(pc) : "memory");
        user_isolated = true;
        verify(true, "EL0 kernel isolation");
        puts("[ok] EL0 cannot read supervisor text\n");
        return;
    }
    if (ec == 0x15 && (esr & 0xffff) == 1) {
        verify(user_result && user_isolated, "EL0 workload completion");
        puts("ARM64 RESULT: "); decimal(checks); puts(" passed, 0 failed\n");
        register u64 x0 __asm__("x0") = 0x84000008;
        __asm__ volatile("hvc #0" : "+r"(x0) : : "memory");
    }
    puts("ARM64 exception ESR="); hex(esr); puts(" FAR="); hex(far); putc('\n');
    fail("unexpected exception");
}
static NORETURN void start_user(void) {
    u64 tensor = page_run(256), stack = page_run(1);
    if (!tensor || !stack) fail("EL0 memory allocation");
    u32 *data = (u32 *)(uptr)tensor;
    for (u32 i = 0; i < 4; ++i) { data[i] = i + 1; data[i + 4] = i + 5; }
    for (u32 i = 0; i < 256; ++i)
        user_l3[i] = (tensor + (u64)i * PAGE) | KDATA | (1u << 6) | 3;
    user_l3[256] = stack | KDATA | (1u << 6) | 3;
    __asm__ volatile("dsb ishst; tlbi vmalle1; dsb ish; isb" ::: "memory");
    u64 sp = 0x20101000, entry = (uptr)arm64_user_start, state = 0x3c0;
    __asm__ volatile("msr vbar_el1, %0; msr sp_el0, %1; msr elr_el1, %2;"
                     "msr spsr_el1, %3; isb; eret"
                     :: "r"((uptr)arm64_vectors), "r"(sp), "r"(entry), "r"(state) : "memory");
    __builtin_unreachable();
}
void arm64_main(uptr dtb) {
    u32 dtb_size = 0;
    parse_fdt(dtb, &dtb_size);
    puts("Nuvora Core ARM64 bring-up | QEMU virt\n");
    puts("[ok] PL011 from FDT: "); hex(uart); putc('\n');
    memory_init(dtb, dtb_size);
    mmu_init();
    verify(free_count > 1024, "available managed RAM");
    puts("[ok] EL1 4 KiB tables, kernel RO/NX, device MMIO\n");
    puts("[ok] managed RAM pages: "); decimal(total_count);
    puts("; free: "); decimal(free_count); putc('\n');
    u64 before = free_count;
    u64 p = page_run(256); /* 1 MiB contiguous buffer for a compute workload. */
    verify(p && free_count == before - 256, "tensor buffer allocation");
    u32 *buf = (u32 *)(uptr)p;
    verify(!buf[0] && !buf[(256 * PAGE) / sizeof(u32) - 1], "tensor buffer zeroing");
    buf[0] = 0x12345678;
    buf[(256 * PAGE) / sizeof(u32) - 1] = 0x87654321;
    verify(buf[0] == 0x12345678 && buf[(256 * PAGE) / sizeof(u32) - 1] == 0x87654321,
           "tensor buffer boundaries");
    page_release(p, 256);
    verify(free_count == before, "tensor buffer reclaim");
    const u32 a[] = {1, 2, 3, 4}, b[] = {5, 6, 7, 8};
    __asm__ volatile("msr cpacr_el1, %0; isb" :: "r"(3ull << 20) : "memory");
    verify(arm64_neon_dot(a, b) == 70, "NEON dot product");
    verify(!page_run((u32)free_count + 1), "impossible allocation");
    puts("[ok] 1 MiB compute buffer reclaimed; NEON dot product = 70\n");
    start_user();
}
