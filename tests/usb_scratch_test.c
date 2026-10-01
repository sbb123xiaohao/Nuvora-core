/* Exercise the actual xHCI DMA layout, including a two-page pointer table
 * and failure rollback. The physical alias matches the kernel's RAM window. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>
#include "../kernel/kernel.h"

enum { DMA_PAGES = 1100 };
static uptr dma_base = 0x08000000u;
static u64 allocation_limit = 0x100000000ull;
static bool used[DMA_PAGES];
static u32 live, allocated, fail_after;
volatile u64 ticks;
uptr page_alloc_below(u64 limit) {
    assert(limit == allocation_limit);
    if (allocated >= fail_after) return 0;
    for (u32 i = 1; i < DMA_PAGES; ++i)
        if (!used[i]) {
            used[i] = true;
            ++allocated;
            ++live;
            uptr p = dma_base + i * PAGE;
            memset(phys_ptr(p), 0, PAGE);
            return p;
        }
    return 0;
}
uptr page_alloc_run_below(u32 count, u64 limit) {
    assert(limit == allocation_limit && count <= 2);
    if (allocated + count > fail_after) return 0;
    for (u32 i = 1; i + count <= DMA_PAGES; ++i) {
        bool free = true;
        for (u32 j = 0; j < count; ++j) free &= !used[i + j];
        if (!free) continue;
        for (u32 j = 0; j < count; ++j) used[i + j] = true;
        allocated += count;
        live += count;
        uptr p = dma_base + i * PAGE;
        memset(phys_ptr(p), 0, count * PAGE);
        return p;
    }
    return 0;
}
void page_free(uptr p) {
    assert(p >= dma_base && (p - dma_base) % PAGE == 0);
    u32 i = (u32)((p - dma_base) / PAGE);
    assert(i < DMA_PAGES && used[i] && live);
    used[i] = false;
    --live;
}
#include "../kernel/usb.c"

static u32 modifier_calls,key_calls;
static u8 previous_modifiers,current_modifiers,last_usage,last_key_modifiers;
void console_usb_modifiers(u8 before,u8 after) {
    ++modifier_calls; previous_modifiers=before; current_modifiers=after;
}
void console_usb_key(u8 usage,u8 modifiers) {
    ++key_calls; last_usage=usage; last_key_modifiers=modifiers;
}
/* A command-completion event must not touch unrelated device paths. */
u32 pci_read(u32 a,u32 o) { (void)a;(void)o;assert(false);return 0; }
void pci_write16(u32 a,u32 o,u16 v) { (void)a;(void)o;(void)v;assert(false); }
void kprintf(const char *s,...) { (void)s;assert(false); }
void console_pointer_report(i32 x,i32 y,u32 b) { (void)x;(void)y;(void)b;assert(false); }
bool pointer_tablet_report(const u8 *p,u32 n,struct nv_pointer_event *e) {
    (void)p;(void)n;(void)e;assert(false);return false;
}
bool pointer_boot_report(const u8 *p,u32 n,struct nv_pointer_event *e) {
    (void)p;(void)n;(void)e;assert(false);return false;
}
void net_usb_receive(const void *p,u32 n) { (void)p;(void)n;assert(false); }
static void check_keyboard_reports(void) {
    allocated=0; fail_after=DMA_PAGES;
    struct device d={.report_page=page_alloc_below(0x100000000ull)};
    assert(d.report_page);
    u8 *report=phys_ptr(d.report_page);
    report[0]=0x08; report[2]=4;
    keyboard_report(&d,1); assert(!modifier_calls && !key_calls);
    keyboard_report(&d,0);
    assert(modifier_calls==1 && !previous_modifiers && current_modifiers==0x08);
    assert(key_calls==1 && last_usage==4 && last_key_modifiers==0x08 && d.repeat_key==4);
    report[0]=0; report[2]=1; /* Release Super during rollover. */
    keyboard_report(&d,0);
    assert(modifier_calls==2 && previous_modifiers==0x08 && !current_modifiers);
    assert(!d.previous[0] && !d.repeat_key && key_calls==1);
    report[2]=0x53; keyboard_report(&d,0);
    assert(last_usage==0x53 && !d.repeat_key); /* Lock keys must not auto-repeat. */
    report[2]=0x39; keyboard_report(&d,0); assert(!d.repeat_key);
    report[2]=report[3]=5; keyboard_report(&d,0);
    assert(key_calls==4 && last_usage==5 && d.repeat_key==5);
    page_free(d.report_page); assert(!live);
}

static void check_ports(void) {
    static u32 regs[MMIO_SIZE / sizeof(u32)];
    struct host h = {.mmio = (volatile u8 *)regs};
    regs[0x14 / 4] = 0x2000;
    regs[0x18 / 4] = 0x1000;
    regs[0x100 / 4] = 2; /* Supported Protocol at extended capability offset. */
    regs[0x108 / 4] = 33 | 32u << 8;
    regs[0x10c / 4] = 3;
    assert(host_capabilities(&h, 0x01000020, 64u << 24 | 16, 0x40u << 16));
    assert(h.info.ports == 64 && h.slot_type[32] == 3 && h.slot_type[63] == 3);
    assert(h.slot_type[31] == 0);
    regs[0x108 / 4] = 255 | 1u << 8;
    assert(host_capabilities(&h, 0x01000020, 255u << 24 | 16, 0x40u << 16));
    assert(h.slot_type[254] == 3);
    regs[0x108 / 4] = 255 | 2u << 8;
    assert(!host_capabilities(&h, 0x01000020, 255u << 24 | 16, 0x40u << 16));
}

static void check(u32 count, u32 budget) {
    struct host h = {0};
    allocated = 0;
    fail_after = budget;
    h.dma_limit = allocation_limit;
    h.dcbaa = page_alloc_below(allocation_limit);
    h.scratch_count = count;
    bool success = budget >= count + 1 + (count > PAGE / sizeof(u64) ? 2u : 1u);
    assert(scratch_alloc(&h) == success);
    if (success) {
        assert(h.scratch_array_pages == (count > PAGE / sizeof(u64) ? 2u : 1u));
        u64 *table = phys_ptr(h.scratch_array);
        assert(((u64 *)phys_ptr(h.dcbaa))[0] == h.scratch_array);
        for (u32 i = 0; i < count; ++i) {
            assert(table[i] == h.scratch[i] && table[i] < allocation_limit);
            assert(used[(table[i] - dma_base) / PAGE]);
        }
    }
    scratch_free(&h);
    page_free(h.dcbaa);
    assert(!live && !h.scratch_array && !h.scratch_array_pages);
}
static void check_high_ring(void) {
    static u32 regs[MMIO_SIZE / sizeof(u32)];
    struct host h = {.mmio=(volatile u8 *)regs, .info.state=NV_USB_RUNNING,
        .runtime=0x1000, .event_cycle=1, .dma_limit=~0ull};
    allocated=0; fail_after=DMA_PAGES;
    assert(ring_init(&h.command, allocation_limit));
    struct trb *ring=phys_ptr(h.command.page);
    assert(h.command.page > 0xffffffffull && ring[RING_TRBS-1].high == 1);
    uptr target=page_alloc_below(allocation_limit);
    h.command_wait=ring_put(&h.command,target,0,0,TYPE(9));
    assert(ring[0].low==(u32)target && ring[0].high==(u32)(target>>32));
    h.event_page=page_alloc_below(allocation_limit);
    struct trb *event=phys_ptr(h.event_page);
    event[0]=(struct trb){(u32)h.command_wait,(u32)(h.command_wait>>32),
        1u<<24,TYPE(33)|1u|(3u<<24)};
    events(&h);
    assert(h.command_code==1 && h.command_slot==3 && h.event_index==1);
    assert(regs[(h.runtime+0x1c)/4]==1);
    page_free(h.command.page); page_free(target); page_free(h.event_page);
    assert(!live);
}
int main(void) {
    check_ports();
    void *mapping = mmap((void *)(PHYS_WINDOW + dma_base), DMA_PAGES * PAGE,
                         PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS |
                         MAP_FIXED_NOREPLACE, -1, 0);
    assert(mapping == (void *)(PHYS_WINDOW + dma_base));
    check(33, DMA_PAGES);
    check(512, DMA_PAGES);
    check(513, DMA_PAGES);
    check(1023, DMA_PAGES);
    check(513, 1);
    check(513, 2);
    check(513, 3);
    check(1023, 70);
    check_keyboard_reports();
    assert(munmap(mapping, DMA_PAGES * PAGE) == 0);
    dma_base=0x100800000ull; allocation_limit=~0ull;
    mapping=mmap((void *)(PHYS_WINDOW+dma_base),DMA_PAGES*PAGE,
        PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(mapping==(void *)(PHYS_WINDOW+dma_base));
    check(513,DMA_PAGES); check(1023,DMA_PAGES); check(513,3);
    check_high_ring();
    assert(munmap(mapping,DMA_PAGES*PAGE)==0);
    puts("PASS USB xHCI: scratchpads, 64-bit DMA/ring completions, rollback, ports and HID");
}
