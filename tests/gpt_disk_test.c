/* Exercise the actual ATA/GPT scanner against a generated sparse disk image. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <nv/abi.h>
#include <nv/string.h>
#define NV_KERNEL_H
#define SNAP_CAP_MAX (16u * 1024u * 1024u)
struct store_layout { u32 slot_lba[2], slot_sectors, snap_cap; };
static FILE *disk_file;
static u64 disk_size, selected_lba;
static u8 high[3], low[3], sector_data[512];
static u32 words;
static bool identifying;
static bool break_primary, break_backup;
static u8 inb(u16 port) { return port == 0x1f7 ? 0x48 : 0; }
static u16 inw(u16 port) {
    assert(port == 0x1f0);
    if (identifying) {
        ++words;
        if (words == 50) return 1u << 9;
        if (words == 84) return 0x4400;
        if (words >= 101 && words <= 104)
            return (u16)(disk_size >> (16 * (words - 101)));
        return 0;
    }
    assert(words < 256);
    u16 value = (u16)sector_data[2 * words] | (u16)sector_data[2 * words + 1] << 8;
    ++words;
    return value;
}
static void outb(u16 port, u8 value) {
    if (port >= 0x1f3 && port <= 0x1f5) {
        u32 index = port - 0x1f3;
        high[index] = low[index]; low[index] = value;
    }
    if (port == 0x1f7 && value == 0xec) { identifying = true; words = 0; }
    if (port == 0x1f7 && (value == 0x24 || value == 0x20)) {
        identifying = false; words = 0;
        selected_lba = (u64)low[0] | (u64)low[1] << 8 | (u64)low[2] << 16;
        if (value == 0x24)
            selected_lba |= (u64)high[0] << 24 | (u64)high[1] << 32 | (u64)high[2] << 40;
        assert(selected_lba < disk_size);
        assert(fseek(disk_file, (long)(selected_lba * 512), SEEK_SET) == 0);
        assert(fread(sector_data, 1, 512, disk_file) == 512);
        if ((selected_lba == 1 && break_primary) ||
            (selected_lba == disk_size - 1 && break_backup))
            sector_data[16] ^= 1; /* corrupt only the in-memory GPT header CRC */
    }
}
static void outw(u16 port, u16 value) { (void)port; (void)value; }
#include "../kernel/disk.c"
int main(int argc, char **argv) {
    assert(argc == 3);
    disk_file = fopen(argv[1], "rb"); assert(disk_file);
    disk_size = (u64)strtoul(argv[2], NULL, 10) * 2048;
    assert(disk_init());
    assert(disk_volume_count() == 2 && volumes[0].start == 2048);
    assert(disk_partition_count() == 2 && disk_volume_partition_number(1) == 2);
    assert(volumes[1].start > volumes[0].start + volumes[0].length - 1);
    assert(volumes[0].geometry.snap_cap == SNAP_CAP_MAX);
    u8 b[512];
    assert(disk_volume_read(1, 0, b) == 0 && !memcmp(b, "NVSTORE2", 8));
    assert(disk_volume_read(0, volumes[0].length, b) == -NV_ENODEV);
    assert(disk_volume_write(0, 0, b) == -NV_EACCESS);
    assert(disk_write(8, b) == -NV_EINVAL); /* GPT metadata is never a slot. */
    volume_count = partition_count = 0;
    break_primary = true;
    assert(!scan_gpt_at(1) && scan_gpt_at(disk_size - 1));
    assert(disk_volume_count() == 2);
    volume_count = partition_count = 0;
    break_backup = true;
    assert(!scan_gpt_at(1) && !scan_gpt_at(disk_size - 1));
    assert(!disk_volume_count());
    fclose(disk_file);
    puts("PASS GPT: partition detection, volume geometry, drive separation and write boundary");
}
