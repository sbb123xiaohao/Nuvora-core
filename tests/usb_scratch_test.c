/* Exercise the actual xHCI DMA layout, including a two-page pointer table
 * and failure rollback. The physical alias matches the kernel's RAM window. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>
#define idle_once privileged_idle_once_unused
#include "../kernel/kernel.h"
#undef idle_once
static void idle_once(void);

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

static u32 modifier_calls,key_calls,modifier_refs[8];
static bool host_failure_fixture;
static u32 pci_command, pci_writes, failure_logs, network_detaches;
static u8 previous_modifiers,current_modifiers,last_usage,last_key_modifiers;
void console_usb_modifiers(u8 before,u8 after) {
    ++modifier_calls; previous_modifiers=before; current_modifiers=after;
    for (u32 i=0;i<8;++i) if ((before^after)&(1u<<i)) {
        if (after&(1u<<i)) ++modifier_refs[i];
        else { assert(modifier_refs[i]); --modifier_refs[i]; }
    }
}
void console_usb_key(u8 usage,u8 modifiers) {
    ++key_calls; last_usage=usage; last_key_modifiers=modifiers;
}
/* A command-completion event must not touch unrelated device paths. */
u32 pci_read(u32 a,u32 o) {
    assert(host_failure_fixture && a==hosts[0].pci && o==4);
    return pci_command;
}
void pci_write16(u32 a,u32 o,u16 v) {
    assert(host_failure_fixture && a==hosts[0].pci && o==4);
    pci_command=v; ++pci_writes;
}
void kprintf(const char *s,...) { (void)s; assert(host_failure_fixture); ++failure_logs; }
void console_pointer_report(i32 x,i32 y,u32 b) { (void)x;(void)y;(void)b;assert(false); }
bool pointer_tablet_report(const u8 *p,u32 n,struct nv_pointer_event *e) {
    (void)p;(void)n;(void)e;assert(false);return false;
}
bool pointer_boot_report(const u8 *p,u32 n,struct nv_pointer_event *e) {
    (void)p;(void)n;(void)e;assert(false);return false;
}
void net_usb_receive(const void *p,u32 n) { (void)p;(void)n;assert(false); }
void net_usb_detach(void) {
    assert(host_failure_fixture && ecm_device && ecm_device->dead &&
           ecm_device->host == &hosts[0] && hosts[0].info.state == NV_USB_FAILED);
    ++network_detaches;
}
/* A compliant control endpoint writes only the reply's actual bytes. It
 * reports a Data Stage short packet only when ISP is set; the separate
 * Status Stage always reports Success with zero residue (xHCI 6.4.1.2). */
static struct device *control_device;
static struct trb *control_setup, *control_data, *control_status;
static u32 control_actual, control_phase;
static const u8 *control_reply;
static void control_event(uptr pointer, u32 code, u32 remaining) {
    struct host *h=control_device->host;
    struct trb *e=phys_ptr(h->event_page+h->event_index*sizeof(*e));
    *e=(struct trb){(u32)pointer,(u32)(pointer>>32),(code<<24)|remaining,
        TYPE(32)|(control_device->slot<<24)|(1u<<16)|h->event_cycle};
}
static void idle_once(void) {
    ++ticks;
    assert(control_device && control_phase<2);
    struct host *h=control_device->host;
    assert(h->transfer_wait==ptr_phys(control_status) && !h->transfer_code);
    if (!control_phase++) {
        if (control_data && (control_data->control&(1u<<16))) {
            assert(control_actual<=control_data->status);
            u8 *buffer=phys_ptr((uptr)control_data->low|((uptr)control_data->high<<32));
            if (control_reply) memcpy(buffer,control_reply,control_actual);
            else memset(buffer,0xa5,control_actual);
            if (control_actual<control_data->status && (control_data->control&(1u<<2)))
                control_event(ptr_phys(control_data),13,control_data->status-control_actual);
        }
    } else {
        /* A short Data Stage event alone must never publish completion. */
        control_event(ptr_phys(control_status),1,0);
    }
}
static struct trb *control_next(struct ring *r, u32 *index) {
    if (*index==RING_TRBS-1) *index=0;
    return (struct trb *)phys_ptr(r->page)+(*index)++;
}
static int control_request(struct device *d, u8 type, u16 length, u32 actual,
                           const u8 *reply, bool string) {
    control_device=d; control_actual=actual; control_phase=0; control_reply=reply;
    u32 index=d->control.index;
    control_setup=control_next(&d->control,&index);
    control_data=length?control_next(&d->control,&index):NULL;
    control_status=control_next(&d->control,&index);
    int got;
    if (string) {
        char out[16]; string_descriptor(d,1,0x409,out,sizeof(out));
        got=(int)strlen(out);
        assert(got==(actual==8?3:0));
    } else {
        got=control(d,type,6,0x100,0,length);
        if (got!=(int)actual)
            fprintf(stderr,"control reply: requested=%u actual=%u returned=%d\n",length,actual,got);
        assert(got==(int)actual);
    }
    assert(control_phase==2 && !d->host->transfer_wait && !d->dead);
    assert((control_setup->control&0x39e)==0 && control_setup->status==8 &&
           (control_setup->control&(1u<<6))); /* Setup reserved bits are zero. */
    assert(((control_setup->control>>16)&3)==(length?(type&128?3u:2u):0u));
    if (control_data) {
        assert(!(control_data->control&((1u<<4)|(1u<<5)))); /* One-TRB Data TD. */
        assert(!!(control_data->control&(1u<<16))==!!(type&128));
        assert(!!(control_data->control&(1u<<2))==!!(type&128));
        if ((type&128) && !reply) {
            const u8 *buffer=phys_ptr(d->buffer);
            for (u32 i=0;i<actual;++i) assert(buffer[i]==0xa5);
            for (u32 i=actual;i<length;++i) assert(!buffer[i]);
        }
    }
    assert((control_status->control&(1u<<5)) && !(control_status->control&(1u<<4)));
    assert(!!(control_status->control&(1u<<16))==(!length || !(type&128)));
    control_device=NULL;
    return got;
}
static void check_control_replies(void) {
    static u32 regs[MMIO_SIZE/sizeof(u32)];
    struct host h={.mmio=(volatile u8 *)regs,.runtime=0x1000,.doorbell=0x2000,
        .info.state=NV_USB_RUNNING,.event_cycle=1};
    allocated=0; fail_after=DMA_PAGES;
    h.event_page=page_alloc_below(allocation_limit);
    struct device d={.host=&h,.slot=7,.buffer=page_alloc_below(allocation_limit)};
    assert(h.event_page && d.buffer && ring_init(&d.control,allocation_limit));
    assert(control_request(&d,0x80,255,18,NULL,false)==18);
    const u8 bad_string[4]={8,3,'A',0};
    control_request(&d,0x80,255,sizeof(bad_string),bad_string,true);
    const u8 good_string[8]={8,3,'A',0,'B',0,'C',0};
    control_request(&d,0x80,255,sizeof(good_string),good_string,true);
    control_request(&d,0x80,1024,16,NULL,false); /* RNDIS-sized read, short reply. */
    control_request(&d,0x80,8,8,NULL,false);
    control_request(&d,0x80,8,0,NULL,false);
    control_request(&d,0x80,PAGE,PAGE,NULL,false);
    control_request(&d,0x80,PAGE,7,NULL,false);
    memset(phys_ptr(d.buffer),0x5a,8);
    control_request(&d,0x21,8,8,NULL,false);
    for (u32 i=0;i<8;++i) assert(((const u8 *)phys_ptr(d.buffer))[i]==0x5a);
    control_request(&d,0,0,0,NULL,false);
    control_request(&d,0x80,0,0,NULL,false);
    /* Cross both transfer/event cycle bits and all three possible positions
     * where the control stages encounter the transfer-ring Link TRB. */
    for (u32 i=0;i<180;++i) control_request(&d,0x80,64,i%64,NULL,false);
    assert(d.control.cycle==1 && h.event_cycle==0);
    page_free(d.control.page); page_free(d.buffer); page_free(h.event_page);
    assert(!live);
}
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

static void check_host_failure(bool network_on_failed_host) {
    static u32 regs[MMIO_SIZE/sizeof(u32)];
    allocated=0; fail_after=DMA_PAGES;
    struct host *failed=&hosts[0], *healthy=&hosts[1];
    *failed=(struct host){.mmio=(volatile u8 *)regs, .op=0x20, .pci=0x2000,
        .info.state=NV_USB_RUNNING, .event_cycle=1};
    healthy->info.state=NV_USB_RUNNING;
    failed->event_page=page_alloc_below(allocation_limit);
    failed->dcbaa=page_alloc_below(allocation_limit);
    assert(failed->event_page && failed->dcbaa);
    regs[failed->op/4]=1; regs[(failed->op+4)/4]=1; /* Halt completion. */
    for (u32 i=0;i<4;++i) {
        struct device *d=&devices[i];
        *d=(struct device){.host=i==2?healthy:failed, .slot=i+1,
            .keyboard_ep=3, .report_page=page_alloc_below(allocation_limit)};
        assert(d->report_page);
        u8 *report=phys_ptr(d->report_page);
        report[0]=i==3?0:0x08; report[2]=4;
        keyboard_report(d,0);
        assert(d->repeat_key==4);
    }
    assert(modifier_refs[3]==3); /* Shared Super: two failed keyboards and one healthy. */
    struct device *network=&devices[4];
    *network=(struct device){.host=network_on_failed_host?failed:healthy, .slot=5,
        .info.state=NV_USB_ETHERNET, .ecm_in_buf=page_alloc_below(allocation_limit),
        .ecm_out_buf=page_alloc_below(allocation_limit)};
    assert(network->ecm_in_buf && network->ecm_out_buf);
    memset(phys_ptr(network->ecm_in_buf),0xa5,PAGE);
    ecm_device=network;
    assert(usb_ecm_link());
    u32 calls=modifier_calls, pages=live;
    pci_command=7; pci_writes=failure_logs=network_detaches=0; host_failure_fixture=true;
    struct trb *event=phys_ptr(failed->event_page);
    event[0]=(struct trb){0,0,21u<<24,TYPE(37)|1u}; /* Host Controller Event: Event Ring Full Error. */
    events(failed);
    assert(failed->info.state==NV_USB_FAILED && pci_command==3 && pci_writes==1);
    assert(!(regs[failed->op/4]&1) && live==pages); /* DMA stays quarantined. */
    if (network_on_failed_host)
        assert(network->dead && !ecm_device && network_detaches==1 && !usb_ecm_link());
    else
        assert(!network->dead && ecm_device==network && !network_detaches && usb_ecm_link());
    assert(((const u8 *)phys_ptr(network->ecm_in_buf))[0]==0xa5 &&
           ((const u8 *)phys_ptr(network->ecm_in_buf))[PAGE-1]==0xa5);
    assert(modifier_calls==calls+2 && modifier_refs[3]==1);
    for (u32 i=0;i<4;++i) {
        struct device *d=&devices[i];
        if (i==2) {
            assert(!d->dead && d->repeat_key==4 && d->previous[0]==0x08);
        } else {
            const u8 released[8]={0};
            assert(d->dead && !d->repeat_key && !memcmp(d->previous,released,8));
        }
    }
    host_failed(failed); events(failed); /* Repeated faults cannot release the healthy owner. */
    assert(modifier_calls==calls+2 && modifier_refs[3]==1 && pci_writes==1 && failure_logs==1);
    assert(network_detaches==(network_on_failed_host?1u:0u));
    ((u8 *)phys_ptr(devices[2].report_page))[0]=0;
    keyboard_report(&devices[2],0);
    assert(!modifier_refs[3]);
    host_failure_fixture=false;
    for (u32 i=0;i<4;++i) { page_free(devices[i].report_page); memset(&devices[i],0,sizeof(devices[i])); }
    page_free(network->ecm_in_buf); page_free(network->ecm_out_buf);
    memset(network,0,sizeof(*network)); ecm_device=NULL;
    page_free(failed->event_page); page_free(failed->dcbaa);
    memset(failed,0,sizeof(*failed)); memset(healthy,0,sizeof(*healthy));
    assert(!live);
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
    check_host_failure(true);
    check_host_failure(false);
    check_control_replies();
    assert(munmap(mapping, DMA_PAGES * PAGE) == 0);
    dma_base=0x100800000ull; allocation_limit=~0ull;
    mapping=mmap((void *)(PHYS_WINDOW+dma_base),DMA_PAGES*PAGE,
        PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(mapping==(void *)(PHYS_WINDOW+dma_base));
    check(513,DMA_PAGES); check(1023,DMA_PAGES); check(513,3);
    check_high_ring();
    check_control_replies();
    assert(munmap(mapping,DMA_PAGES*PAGE)==0);
    puts("PASS USB xHCI: scratchpads, 64-bit DMA/ring completions, rollback, ports, HID, fault cleanup and control short replies/stages/ring wrap");
}
