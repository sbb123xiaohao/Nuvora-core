#include "kernel.h"
static bool identified, owned;
static u32 sectors;
static void delay400(void) {
    for (u32 i = 0; i < 4; ++i)
        (void)inb(0x3f6);
}
static int wait_ready(bool data) {
    for (u32 i = 0; i < 1000000; ++i) {
        u8 s = inb(0x1f7);
        if (!s || s == 0xff)
            return -NV_ENODEV;
        if (s & 0x80)
            continue;
        if (s & 0x21)
            return -NV_EIO;
        if (!data || (s & 8))
            return 0;
    }
    return -NV_EIO;
}
static int command(u32 lba, u8 op) {
    if (!identified || lba >= sectors || lba >= 0x10000000u)
        return -NV_ENODEV;
    if (wait_ready(false) < 0)
        return -NV_EIO;
    outb(0x1f6, (u8)(0xe0 | ((lba >> 24) & 15)));
    delay400();
    outb(0x1f2, 1);
    outb(0x1f3, (u8)lba);
    outb(0x1f4, (u8)(lba >> 8));
    outb(0x1f5, (u8)(lba >> 16));
    outb(0x1f7, op);
    delay400();
    return wait_ready(true);
}
int disk_read(u32 lba, void *buf) {
    int r = command(lba, 0x20);
    if (r < 0)
        return r;
    u16 *p = buf;
    for (u32 i = 0; i < 256; ++i)
        p[i] = inw(0x1f0);
    delay400();
    return wait_ready(false);
}
int disk_write(u32 lba, const void *buf) {
    if (!owned)
        return -NV_EACCESS;
    if (!((lba >= 8 && lba < 8 + 2049) || (lba >= 4096 && lba < 4096 + 2049)))
        return -NV_EINVAL;
    int r = command(lba, 0x30);
    if (r < 0)
        return r;
    const u16 *p = buf;
    for (u32 i = 0; i < 256; ++i)
        outw(0x1f0, p[i]);
    delay400();
    return wait_ready(false);
}
int disk_flush(void) {
    if (!owned)
        return -NV_ENODEV;
    if (wait_ready(false) < 0)
        return -NV_EIO;
    outb(0x1f7, 0xe7);
    delay400();
    return wait_ready(false);
}
bool disk_ready(void) {
    return owned;
}
bool disk_init(void) {
    outb(0x3f6, 2);
    outb(0x1f6, 0xa0);
    delay400();
    outb(0x1f2, 0);
    outb(0x1f3, 0);
    outb(0x1f4, 0);
    outb(0x1f5, 0);
    outb(0x1f7, 0xec);
    delay400();
    if (wait_ready(true) < 0 || inb(0x1f4) || inb(0x1f5))
        return false;
    u16 id[256];
    for (u32 i = 0; i < 256; ++i)
        id[i] = inw(0x1f0);
    delay400();
    if (!(id[49] & (1u << 9)))
        return false;
    sectors = (u32)id[60] | ((u32)id[61] << 16);
    if (sectors < 8192)
        return false;
    identified = true;
    u8 header[512];
    if (disk_read(0, header) < 0)
        return false;
    u32 fields[6];
    memcpy(fields, header + 8, sizeof(fields));
    if (memcmp(header, "NVSTORE1", 8) || fields[0] != 1 || fields[1] != 512 || fields[2] != 8 ||
        fields[3] != 4096 || fields[4] != 2049 || fields[5] != crc32(header, 28))
        return false;
    owned = true;
    return true;
}
