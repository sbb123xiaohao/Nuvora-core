#include "kernel.h"

/* Original, bounded xHCI driver. All DMA memory is page-aligned and below 4 GiB.
 * Polling runs in kernel thread/syscall context, never in an interrupt handler.
 * Device DMA pages are released only after Disable Slot has completed. */
#define MMIO_SIZE 65536u
#define USB_DMA_LIMIT 0x100000000ull /* conservative DMA mask, like Linux dma_alloc_coherent */
#define MAX_PORTS 32u
#define MAX_SCRATCH 32u
#define RING_TRBS 256u
#define TYPE(n) ((u32)(n) << 10)
#define PORT_CHANGES 0x00fe0000u
struct trb {
    u32 low, high, status, control;
};
struct ring {
    u32 page, index, cycle;
};
struct host {
    struct nv_usb_controller info;
    volatile u8 *mmio;
    u32 op, runtime, doorbell, context_size, slots, pci;
    u32 dcbaa, event_page, erst, scratch_array, scratch[MAX_SCRATCH], scratch_count;
    struct ring command;
    u32 event_index, event_cycle, command_wait, command_code, command_slot;
    u32 transfer_wait, transfer_slot, transfer_code, short_left;
    u32 attempted[MAX_PORTS], slot_type[MAX_PORTS];
    bool changed;
};
struct device {
    struct nv_usb_device info;
    struct host *host;
    u32 slot, root_port, route, depth, tt, max_packet;
    u32 input, output, buffer, report_page, mouse_page;
    struct ring control, interrupt, mouse_ring;
    struct ring ecm_in, ecm_out;
    struct ring cdc_in, cdc_out;
    u32 ecm_in_buf, ecm_out_buf, ecm_in_ep, ecm_out_ep;
    u32 ecm_in_packet, ecm_out_packet, ecm_interface, ecm_control;
    u32 ecm_in_wait, ecm_out_wait;
    u32 cdc_in_buf, cdc_out_buf, cdc_in_ep, cdc_out_ep;
    u32 cdc_in_packet, cdc_out_packet, cdc_interface, cdc_control;
    u32 cdc_in_wait, cdc_out_wait;
    u8 ecm_mac[6];
    u32 keyboard_ep, keyboard_interface, keyboard_packet, keyboard_interval;
    u32 mouse_ep, mouse_interface, mouse_packet, mouse_interval, mouse_wait;
    u32 interrupt_wait, repeat_at, hub_ports, hub_ttt;
    u32 attempted[15];
    u8 previous[8], repeat_key;
    bool dead, multi_tt;
};
static struct host hosts[NV_USB_CONTROLLER_MAX];
static struct device devices[NV_USB_DEVICE_MAX];
static struct device *ecm_device;
static struct device *mouse_device;
static char wifi_response[2048];
static u32 wifi_response_size;
static u32 host_count, last_scan;
static bool initialized, busy;

static void barrier(void) {
    __asm__ volatile("" ::: "memory");
}
static u32 read32(struct host *h, u32 offset) {
    return *(volatile u32 *)(h->mmio + offset);
}
static void write32(struct host *h, u32 offset, u32 value) {
    *(volatile u32 *)(h->mmio + offset) = value;
}
static void write64(struct host *h, u32 offset, u64 value) {
    write32(h, offset, (u32)value);
    write32(h, offset + 4, (u32)(value >> 32));
}
static void delay_ms(u32 ms) {
    u32 start = ticks, duration = (ms + 9) / 10;
    while (ticks - start < duration)
        idle_once();
}
static bool wait_bits(struct host *h, u32 off, u32 mask, u32 value, u32 ms) {
    u32 start = ticks;
    do {
        if ((read32(h, off) & mask) == value)
            return true;
        idle_once();
    } while (ticks - start < (ms + 9) / 10);
    return false;
}
static void host_failed(struct host *h) {
    if (h->info.state == NV_USB_FAILED)
        return;
    h->info.state = NV_USB_FAILED;
    write32(h, h->op, read32(h, h->op) & ~1u);
    /* Keep DMA allocations quarantined on a controller error. Even a broken
       controller must never be given pages subsequently used by a process. */
    wait_bits(h, h->op + 4, 1, 1, 100);
    pci_write16(h->pci, 4, (u16)(pci_read(h->pci, 4) & ~4u));
    for (u32 i = 0; i < ARRAY_LEN(devices); ++i)
        if (devices[i].host == h) {
            devices[i].dead = true;
            devices[i].repeat_key = 0;
        }
    if (mouse_device && mouse_device->host == h) {
        console_pointer_report(0, 0, 0);
        mouse_device = NULL;
    }
    kprintf("[usb] controller %u stopped after an error\n", (u32)(h - hosts));
}
static bool ring_init(struct ring *r) {
    if (!r->page)
        r->page = page_alloc_below(USB_DMA_LIMIT);
    if (!r->page)
        return false;
    memset(phys_ptr(r->page), 0, PAGE);
    r->index = 0;
    r->cycle = 1;
    struct trb *t = phys_ptr(r->page);
    t[RING_TRBS - 1] = (struct trb){r->page, 0, 0, TYPE(6) | 2};
    return true;
}
static u32 ring_put(struct ring *r, u32 low, u32 high, u32 status, u32 control) {
    volatile struct trb *t = phys_ptr(r->page);
    if (r->index == RING_TRBS - 1) {
        t[r->index].control = TYPE(6) | 2 | r->cycle;
        r->cycle ^= 1;
        r->index = 0;
    }
    u32 address = r->page + r->index * sizeof(struct trb);
    t[r->index].low = low;
    t[r->index].high = high;
    t[r->index].status = status;
    barrier();
    t[r->index].control = control | r->cycle;
    ++r->index;
    return address;
}
static void keyboard_arm(struct device *d) {
    if (d->dead || !d->keyboard_ep || d->host->info.state != NV_USB_RUNNING)
        return;
    memset(phys_ptr(d->report_page), 0, 8);
    d->interrupt_wait = ring_put(&d->interrupt, d->report_page, 0, 8, TYPE(1) | (1u << 5));
    barrier();
    write32(d->host, d->host->doorbell + d->slot * 4, d->keyboard_ep);
}
static void mouse_arm(struct device *d) {
    if (d->dead || !d->mouse_ep || d->host->info.state != NV_USB_RUNNING) return;
    memset(phys_ptr(d->mouse_page), 0, 3);
    d->mouse_wait = ring_put(&d->mouse_ring, d->mouse_page, 0, 3, TYPE(1) | (1u << 5));
    barrier();
    write32(d->host, d->host->doorbell + d->slot * 4, d->mouse_ep);
}
/* HID boot protocol: three bytes of buttons, signed X and signed Y. Other
 * HID report layouts require descriptor parsing and must not reach here. */
static void mouse_report(struct device *d, u32 remaining) {
    if (remaining) return;
    barrier();
    const u8 *report = phys_ptr(d->mouse_page);
    struct nv_pointer_event event;
    if (!pointer_boot_report(report, 3, &event)) return;
    console_pointer_report(event.dx, event.dy, event.buttons);
    ++d->info.reports;
}
static void ecm_arm(struct device *d) {
    if (d->dead || !d->ecm_in_ep || d->host->info.state != NV_USB_RUNNING) return;
    d->ecm_in_wait = ring_put(&d->ecm_in, d->ecm_in_buf, 0, 2048,
                              TYPE(1) | (1u << 5));
    barrier();
    write32(d->host, d->host->doorbell + d->slot * 4, d->ecm_in_ep);
}
static void cdc_arm(struct device *d) {
    if (d->dead || !d->cdc_in_ep || d->host->info.state != NV_USB_RUNNING) return;
    d->cdc_in_wait = ring_put(&d->cdc_in, d->cdc_in_buf, 0, 512,
                              TYPE(1) | (1u << 5));
    barrier();
    write32(d->host, d->host->doorbell + d->slot * 4, d->cdc_in_ep);
}
static void keyboard_report(struct device *d, u32 remaining) {
    if (remaining > 0)
        return; /* A boot-keyboard report is exactly eight bytes. */
    barrier();
    const u8 *report = phys_ptr(d->report_page);
    for (u32 i = 2; i < 8; ++i)
        if (report[i] >= 1 && report[i] <= 3)
            return; /* Rollover/POST error, not a list of real key presses. */
    for (u32 i = 2; i < 8; ++i) {
        u8 key = report[i];
        if (!key)
            continue;
        bool old = false;
        for (u32 j = 2; j < 8; ++j)
            if (d->previous[j] == key || (j < i && report[j] == key))
                old = true;
        if (!old) {
            console_usb_key(key, report[0]);
            d->repeat_key = key == 0x39 ? 0 : key;
            d->repeat_at = ticks + 50;
        }
    }
    bool held = false;
    for (u32 i = 2; i < 8; ++i)
        if (report[i] && report[i] == d->repeat_key)
            held = true;
    if (!held)
        d->repeat_key = 0;
    memcpy(d->previous, report, 8);
    ++d->info.reports;
}
static void events(struct host *h) {
    if (h->info.state != NV_USB_RUNNING)
        return;
    volatile struct trb *ring = phys_ptr(h->event_page);
    for (u32 count = 0; count < RING_TRBS; ++count) {
        volatile struct trb *e = &ring[h->event_index];
        if ((e->control & 1) != h->event_cycle)
            break;
        barrier();
        u32 low = e->low, high = e->high, status = e->status, control = e->control;
        u32 type = (control >> 10) & 63, code = status >> 24;
        u32 slot = control >> 24, endpoint = (control >> 16) & 31;
        if (type == 33 && !high && low == h->command_wait) {
            h->command_code = code;
            h->command_slot = slot;
        } else if (type == 32 && !high) {
            if (slot == h->transfer_slot && endpoint == 1 && h->transfer_wait) {
                if (code == 13)
                    h->short_left = status & 0xffffff;
                if (low == h->transfer_wait || (code != 1 && code != 13))
                    h->transfer_code = code;
            }
            for (u32 i = 0; i < ARRAY_LEN(devices); ++i) {
                struct device *d = &devices[i];
                if (d->host != h || d->slot != slot || !d->keyboard_ep ||
                    endpoint != d->keyboard_ep || low != d->interrupt_wait)
                    continue;
                d->interrupt_wait = 0;
                if (code == 1 || code == 13) {
                    keyboard_report(d, status & 0xffffff);
                    keyboard_arm(d);
                } else {
                    d->repeat_key = 0;
                    d->dead = true;
                    h->changed = true;
                }
            }
            struct device *mouse = mouse_device;
            if (mouse && !mouse->dead && mouse->host == h && mouse->slot == slot &&
                endpoint == mouse->mouse_ep && low == mouse->mouse_wait) {
                mouse->mouse_wait = 0;
                if (code == 1 || code == 13) {
                    mouse_report(mouse, status & 0xffffff);
                    mouse_arm(mouse);
                } else {
                    mouse->dead = true;
                    console_pointer_report(0, 0, 0);
                    h->changed = true;
                }
            }
            struct device *d = ecm_device;
            if (d && d->host == h && d->slot == slot && !d->dead) {
                if (endpoint == d->ecm_in_ep && low == d->ecm_in_wait) {
                    d->ecm_in_wait = 0;
                    if (code == 1 || code == 13) {
                        u32 length = 2048 - MIN(status & 0xffffffu, 2048u);
                        if (length >= 14 && length <= 1514)
                            net_usb_receive(phys_ptr(d->ecm_in_buf), length);
                        ecm_arm(d);
                    } else { d->dead = true; h->changed = true; }
                }
                if (endpoint == d->ecm_out_ep && low == d->ecm_out_wait) {
                    d->ecm_out_wait = 0;
                    if (code != 1 && code != 13) { d->dead = true; h->changed = true; }
                }
                if (d->cdc_in_ep && endpoint == d->cdc_in_ep && low == d->cdc_in_wait) {
                    d->cdc_in_wait = 0;
                    if (code == 1 || code == 13) {
                        u32 got = 512 - MIN(status & 0xffffffu, 512u);
                        const u8 *s = phys_ptr(d->cdc_in_buf);
                        for (u32 j = 0; j < got && wifi_response_size < sizeof(wifi_response); ++j)
                            if (s[j] == '\n' || s[j] == '\r' || (s[j] >= 32 && s[j] < 127))
                                wifi_response[wifi_response_size++] = (char)s[j];
                        cdc_arm(d);
                    } else { d->dead = true; h->changed = true; }
                }
                if (d->cdc_out_ep && endpoint == d->cdc_out_ep && low == d->cdc_out_wait) {
                    d->cdc_out_wait = 0;
                    memset(phys_ptr(d->cdc_out_buf), 0, PAGE);
                    if (code != 1 && code != 13) { d->dead = true; h->changed = true; }
                }
            }
        } else if (type == 34)
            h->changed = true;
        else if (type == 37) {
            host_failed(h);
            return;
        }
        if (++h->event_index == RING_TRBS) {
            h->event_index = 0;
            h->event_cycle ^= 1;
        }
        write64(h, h->runtime + 0x18, (h->event_page + h->event_index * 16) | 8u);
    }
}
static int command(struct host *h, u32 type, u32 pointer, u32 flags) {
    if (h->info.state != NV_USB_RUNNING)
        return -NV_ENODEV;
    h->command_code = h->command_slot = 0;
    h->command_wait = ring_put(&h->command, pointer, 0, 0, TYPE(type) | flags);
    barrier();
    write32(h, h->doorbell, 0);
    u32 start = ticks;
    while (!h->command_code && ticks - start < 100) {
        events(h);
        if (!h->command_code)
            idle_once();
    }
    h->command_wait = 0;
    if (!h->command_code) {
        host_failed(h);
        return -NV_EIO;
    }
    return h->command_code == 1 ? (int)h->command_slot : -NV_EIO;
}
static bool control_recover(struct device *d, bool halted) {
    struct host *h = d->host;
    if (command(h, halted ? 14 : 15, 0, (d->slot << 24) | (1u << 16)) < 0)
        return false;
    ring_init(&d->control);
    return command(h, 16, d->control.page | 1, (d->slot << 24) | (1u << 16)) >= 0;
}
static int control(struct device *d, u8 request_type, u8 request, u16 value, u16 index,
                   u16 length) {
    if (d->dead || length > PAGE || d->host->info.state != NV_USB_RUNNING)
        return -NV_ENODEV;
    struct host *h = d->host;
    bool in = (request_type & 128) != 0;
    if (in)
        memset(phys_ptr(d->buffer), 0, length);
    u32 setup_low = request_type | ((u32)request << 8) | ((u32)value << 16);
    u32 setup_high = index | ((u32)length << 16);
    ring_put(&d->control, setup_low, setup_high, 8,
             TYPE(2) | (1u << 6) | (length ? 1u << 4 : 0) | (length ? (in ? 3u : 2u) << 16 : 0));
    if (length)
        ring_put(&d->control, d->buffer, 0, length, TYPE(3) | (1u << 4) | (in ? 1u << 16 : 0));
    h->transfer_code = h->short_left = 0;
    h->transfer_slot = d->slot;
    h->transfer_wait =
        ring_put(&d->control, 0, 0, 0, TYPE(4) | (1u << 5) | ((!length || !in) ? 1u << 16 : 0));
    barrier();
    write32(h, h->doorbell + d->slot * 4, 1);
    u32 start = ticks;
    while (!h->transfer_code && ticks - start < 100 && h->info.state == NV_USB_RUNNING) {
        events(h);
        if (!h->transfer_code)
            idle_once();
    }
    h->transfer_wait = 0;
    u32 code = h->transfer_code;
    if (code != 1 && code != 13) {
        if (!control_recover(d, code != 0))
            d->dead = true;
        return -NV_EIO;
    }
    barrier();
    return (int)(length - MIN((u32)length, h->short_left));
}
static u32 *input_context(struct device *d, u32 index) {
    return phys_ptr(d->input + index * d->host->context_size);
}
static void slot_context(struct device *d, u32 entries) {
    u32 *slot = input_context(d, 1);
    slot[0] = d->route | (d->info.speed << 20) | (entries << 27) | (d->hub_ports ? 1u << 26 : 0) |
              (d->multi_tt ? 1u << 25 : 0);
    slot[1] = (d->root_port << 16) | (d->hub_ports << 24);
    slot[2] = d->tt | (d->hub_ttt << 16);
}
static int descriptor(struct device *d, u8 type, u8 index, u16 language, u16 length) {
    return control(d, 0x80, 6, ((u16)type << 8) | index, language, length);
}
static void string_descriptor(struct device *d, u8 index, u16 language, char *out, u32 cap) {
    out[0] = 0;
    if (!index)
        return;
    int received = descriptor(d, 3, index, language, 255);
    if (received < 2)
        return;
    const u8 *s = phys_ptr(d->buffer);
    u32 length = s[0];
    if (s[1] != 3 || length < 2 || length > (u32)received || (length & 1))
        return;
    u32 pos = 0;
    for (u32 i = 2; i + 1 < length && pos + 1 < cap; i += 2) {
        u16 ch = s[i] | ((u16)s[i + 1] << 8);
        out[pos++] = ch >= 32 && ch < 127 ? (char)ch : '?';
    }
    out[pos] = 0;
}
static int configure_keyboard(struct device *d) {
    if (!d->keyboard_ep)
        return 0;
    if (!ring_init(&d->interrupt) || !(d->report_page = page_alloc_below(USB_DMA_LIMIT)))
        return -NV_ENOMEM;
    memset(phys_ptr(d->input), 0, PAGE);
    input_context(d, 0)[1] = 1 | (1u << d->keyboard_ep);
    slot_context(d, d->keyboard_ep);
    u32 *ep = input_context(d, d->keyboard_ep + 1);
    u32 interval = d->keyboard_interval;
    if (d->info.speed >= 3)
        interval = MAX(1u, MIN(interval, 16u)) - 1;
    else {
        u32 value = MAX(1u, interval), shift = 0;
        while (value >>= 1)
            ++shift;
        interval = MIN(shift + 3, 10u);
    }
    ep[0] = interval << 16;
    ep[1] = (3u << 1) | (7u << 3) | (d->keyboard_packet << 16);
    ep[2] = d->interrupt.page | 1;
    ep[4] = 8 | (d->keyboard_packet << 16);
    if (command(d->host, 12, d->input, d->slot << 24) < 0 ||
        control(d, 0x21, 11, 0, (u16)d->keyboard_interface, 0) < 0)
        return -NV_EIO;
    /* SET_IDLE is optional; keyboards which STALL it are recovered at EP0. */
    control(d, 0x21, 10, 0, (u16)d->keyboard_interface, 0);
    if (d->dead)
        return -NV_EIO;
    d->info.state = NV_USB_KEYBOARD;
    keyboard_arm(d);
    return 0;
}
static int configure_mouse(struct device *d) {
    if (!d->mouse_ep || mouse_device) return 0;
    if (!ring_init(&d->mouse_ring) ||
        !(d->mouse_page = page_alloc_below(USB_DMA_LIMIT))) return -NV_ENOMEM;
    memset(phys_ptr(d->input), 0, PAGE);
    input_context(d, 0)[1] = 1u | (1u << d->mouse_ep);
    slot_context(d, MAX(d->keyboard_ep, d->mouse_ep));
    u32 *ep = input_context(d, d->mouse_ep + 1);
    u32 interval = d->mouse_interval;
    if (d->info.speed >= 3) interval = MAX(1u, MIN(interval, 16u)) - 1;
    else {
        u32 value = MAX(1u, interval), shift = 0;
        while (value >>= 1) ++shift;
        interval = MIN(shift + 3, 10u);
    }
    ep[0] = interval << 16;
    ep[1] = (3u << 1) | (7u << 3) | (d->mouse_packet << 16);
    ep[2] = d->mouse_ring.page | 1u;
    ep[4] = 3u | (d->mouse_packet << 16);
    if (command(d->host, 12, d->input, d->slot << 24) < 0 ||
        control(d, 0x21, 11, 0, (u16)d->mouse_interface, 0) < 0)
        return -NV_EIO;
    control(d, 0x21, 10, 0, (u16)d->mouse_interface, 0);
    if (d->dead) return -NV_EIO;
    d->info.state = d->keyboard_ep ? NV_USB_COMPOSITE_INPUT : NV_USB_MOUSE;
    mouse_device = d;
    mouse_arm(d);
    return 0;
}
static int configure_ecm(struct device *d) {
    if (!d->ecm_in_ep || !d->ecm_out_ep || ecm_device) return 0;
    if (!ring_init(&d->ecm_in) || !ring_init(&d->ecm_out) ||
        !(d->ecm_in_buf = page_alloc_below(USB_DMA_LIMIT)) ||
        !(d->ecm_out_buf = page_alloc_below(USB_DMA_LIMIT))) return -NV_ENOMEM;
    memset(phys_ptr(d->input), 0, PAGE);
    u32 highest = MAX(d->ecm_in_ep, d->ecm_out_ep);
    input_context(d, 0)[1] = 1u | (1u << d->ecm_in_ep) | (1u << d->ecm_out_ep);
    if (d->cdc_in_ep && d->cdc_out_ep && d->info.vendor == 0x303a) {
        if (!ring_init(&d->cdc_in) || !ring_init(&d->cdc_out) ||
            !(d->cdc_in_buf = page_alloc_below(USB_DMA_LIMIT)) ||
            !(d->cdc_out_buf = page_alloc_below(USB_DMA_LIMIT))) return -NV_ENOMEM;
        highest = MAX(highest, MAX(d->cdc_in_ep, d->cdc_out_ep));
        input_context(d, 0)[1] |= (1u << d->cdc_in_ep) | (1u << d->cdc_out_ep);
        u32 *ci = input_context(d, d->cdc_in_ep + 1);
        ci[1] = (3u << 1) | (6u << 3) | (d->cdc_in_packet << 16);
        ci[2] = d->cdc_in.page | 1u;
        ci[4] = 512;
        u32 *co = input_context(d, d->cdc_out_ep + 1);
        co[1] = (3u << 1) | (2u << 3) | (d->cdc_out_packet << 16);
        co[2] = d->cdc_out.page | 1u;
        co[4] = 512;
    } else d->cdc_in_ep = d->cdc_out_ep = 0;
    slot_context(d, highest);
    u32 *in = input_context(d, d->ecm_in_ep + 1);
    in[1] = (3u << 1) | (6u << 3) | (d->ecm_in_packet << 16);
    in[2] = d->ecm_in.page | 1u;
    in[4] = 2048u;
    u32 *out = input_context(d, d->ecm_out_ep + 1);
    out[1] = (3u << 1) | (2u << 3) | (d->ecm_out_packet << 16);
    out[2] = d->ecm_out.page | 1u;
    out[4] = 2048u;
    if (command(d->host, 12, d->input, d->slot << 24) < 0 ||
        control(d, 1, 11, 1, (u16)d->ecm_interface, 0) < 0 ||
        control(d, 0x21, 0x43, 3, (u16)d->ecm_control, 0) < 0)
        return -NV_EIO;
    if (d->cdc_in_ep) {
        u8 *line = phys_ptr(d->buffer);
        line[0] = 0; line[1] = 0xc2; line[2] = 1; line[3] = 0; /* 115200 */
        line[4] = 0; line[5] = 0; line[6] = 8;
        if (control(d, 0x21, 0x20, 0, (u16)d->cdc_control, 7) < 0 ||
            control(d, 0x21, 0x22, 3, (u16)d->cdc_control, 0) < 0)
            d->cdc_in_ep = d->cdc_out_ep = 0;
    }
    d->info.state = NV_USB_ETHERNET;
    ecm_device = d;
    net_usb_attach(d->info.vendor, d->info.product, d->ecm_mac);
    ecm_arm(d);
    if (d->cdc_in_ep) cdc_arm(d);
    return 0;
}
static int nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static int configure_hub(struct device *d) {
    if (d->depth >= 5)
        return 0;
    bool superspeed = d->info.speed >= 4;
    if (control(d, 0xa0, 6, superspeed ? 0x2a00 : 0x2900, 0, 12) < 7)
        return -NV_EIO;
    const u8 *data = phys_ptr(d->buffer);
    if (data[1] != (superspeed ? 0x2a : 0x29) || data[0] < 7 || !data[2] || data[2] > 15)
        return -NV_EINVAL;
    d->hub_ports = data[2];
    d->hub_ttt = d->info.speed == 3 ? (data[3] >> 5) & 3 : 0;
    d->multi_tt = d->info.speed == 3 && d->info.protocol == 2;
    u32 power_delay = data[5] * 2;
    memset(phys_ptr(d->input), 0, PAGE);
    input_context(d, 0)[1] = 1;
    slot_context(d, 1);
    if (command(d->host, 12, d->input, d->slot << 24) < 0)
        return -NV_EIO;
    if (superspeed && control(d, 0x20, 12, (u16)d->depth, 0, 0) < 0)
        return -NV_EIO;
    for (u32 port = 1; port <= d->hub_ports; ++port)
        if (control(d, 0x23, 3, 8, (u16)port, 0) < 0)
            return -NV_EIO;
    delay_ms(MAX(100u, power_delay));
    d->info.state = NV_USB_HUB;
    return 0;
}
static int identify(struct device *d) {
    u8 desc[18];
    if (descriptor(d, 1, 0, 0, 8) < 8)
        return -NV_EIO;
    memcpy(desc, phys_ptr(d->buffer), 8);
    if (desc[0] != 18 || desc[1] != 1)
        return -NV_EINVAL;
    u32 packet = d->info.speed >= 4 ? (desc[7] == 9 ? 512u : 0) : desc[7];
    if ((packet != 8 && packet != 16 && packet != 32 && packet != 64 && packet != 512) ||
        (d->info.speed == 2 && packet != 8) || (d->info.speed == 3 && packet != 64))
        return -NV_EINVAL;
    if (packet != d->max_packet) {
        memset(phys_ptr(d->input), 0, PAGE);
        input_context(d, 0)[1] = 2;
        input_context(d, 2)[1] = packet << 16;
        if (command(d->host, 13, d->input, d->slot << 24) < 0)
            return -NV_EIO;
        d->max_packet = packet;
    }
    if (descriptor(d, 1, 0, 0, sizeof(desc)) < (int)sizeof(desc))
        return -NV_EIO;
    memcpy(desc, phys_ptr(d->buffer), sizeof(desc));
    d->info.vendor = desc[8] | ((u32)desc[9] << 8);
    d->info.product = desc[10] | ((u32)desc[11] << 8);
    d->info.usb_version = desc[2] | ((u32)desc[3] << 8);
    d->info.class_code = desc[4];
    d->info.subclass = desc[5];
    d->info.protocol = desc[6];
    d->info.state = NV_USB_IDENTIFIED;
    if (!desc[17] || descriptor(d, 2, 0, 0, 9) < 9)
        return -NV_EIO;
    const u8 *cfg = phys_ptr(d->buffer);
    u32 length = cfg[2] | ((u32)cfg[3] << 8);
    if (cfg[0] != 9 || cfg[1] != 2 || length < 9 || length > PAGE || !cfg[5])
        return -NV_EINVAL;
    if (descriptor(d, 2, 0, 0, (u16)length) < (int)length)
        return -NV_EIO;
    if (cfg[0] != 9 || cfg[1] != 2 || (cfg[2] | ((u32)cfg[3] << 8)) != length || !cfg[5])
        return -NV_EINVAL;
    u8 config_value = cfg[5];
    d->info.interfaces = cfg[4];
    bool keyboard_interface = false, mouse_interface = false, first_interface = true;
    bool ecm_control = false, ecm_data = false, cdc_control = false, cdc_data = false;
    u8 ecm_mac_string = 0;
    u32 interface_number = 0;
    for (u32 off = 0; off < length;) {
        u32 len = cfg[off];
        if (len < 2 || len > length - off)
            return -NV_EINVAL;
        if (cfg[off + 1] == 4) {
            if (len < 9)
                return -NV_EINVAL;
            keyboard_interface =
                cfg[off + 3] == 0 && cfg[off + 5] == 3 && cfg[off + 6] == 1 && cfg[off + 7] == 1;
            mouse_interface =
                cfg[off + 3] == 0 && cfg[off + 5] == 3 && cfg[off + 6] == 1 && cfg[off + 7] == 2;
            interface_number = cfg[off + 2];
            if (cfg[off + 5] == 2 && cfg[off + 6] == 6 && cfg[off + 3] == 0) {
                ecm_control = true; d->ecm_control = interface_number;
            }
            if (cfg[off + 5] == 2 && cfg[off + 6] == 2 && cfg[off + 3] == 0) {
                cdc_control = true; d->cdc_control = interface_number;
            }
            ecm_data = ecm_control && cfg[off + 5] == 10 && cfg[off + 3] == 1;
            cdc_data = cdc_control && cfg[off + 5] == 10 && cfg[off + 3] == 0;
            if (ecm_data) d->ecm_interface = interface_number;
            if (cdc_data) d->cdc_interface = interface_number;
            if (first_interface && !d->info.class_code) {
                d->info.class_code = cfg[off + 5];
                d->info.subclass = cfg[off + 6];
                d->info.protocol = cfg[off + 7];
            }
            first_interface = false;
        } else if (cfg[off + 1] == 0x24 && ecm_control &&
                   !ecm_data && len >= 13 && cfg[off + 2] == 0x0f) {
            ecm_mac_string = cfg[off + 3];
        } else if (cfg[off + 1] == 5) {
            if (len < 7)
                return -NV_EINVAL;
            u32 ep = cfg[off + 2], size = cfg[off + 4] | ((u32)cfg[off + 5] << 8);
            if (keyboard_interface && !d->keyboard_ep && (ep & 0x80) && (ep & 15) && !(ep & 0x70) &&
                (cfg[off + 3] & 3) == 3 && size >= 8 && size <= 64) {
                d->keyboard_ep = (ep & 15) * 2 + 1;
                d->keyboard_interface = interface_number;
                d->keyboard_packet = size;
                d->keyboard_interval = cfg[off + 6];
            }
            if (mouse_interface && !d->mouse_ep && (ep & 0x80) && (ep & 15) && !(ep & 0x70) &&
                (cfg[off + 3] & 3) == 3 && size >= 3 && size <= 64) {
                d->mouse_ep = (ep & 15) * 2 + 1;
                d->mouse_interface = interface_number;
                d->mouse_packet = size;
                d->mouse_interval = cfg[off + 6];
            }
            if (ecm_data && (ep & 15) && !(ep & 0x70) && (cfg[off + 3] & 3) == 2 &&
                (size == 64 || size == 512 || size == 1024)) {
                if ((ep & 0x80) && !d->ecm_in_ep) {
                    d->ecm_in_ep = (ep & 15) * 2 + 1;
                    d->ecm_in_packet = size;
                } else if (!(ep & 0x80) && !d->ecm_out_ep) {
                    d->ecm_out_ep = (ep & 15) * 2;
                    d->ecm_out_packet = size;
                }
            }
            if (cdc_data && (ep & 15) && !(ep & 0x70) && (cfg[off + 3] & 3) == 2 &&
                (size == 64 || size == 512 || size == 1024)) {
                if ((ep & 0x80) && !d->cdc_in_ep) {
                    d->cdc_in_ep = (ep & 15) * 2 + 1;
                    d->cdc_in_packet = size;
                } else if (!(ep & 0x80) && !d->cdc_out_ep) {
                    d->cdc_out_ep = (ep & 15) * 2;
                    d->cdc_out_packet = size;
                }
            }
        }
        off += len;
    }
    u16 language = 0x0409;
    if ((desc[14] || desc[15] || desc[16]) && descriptor(d, 3, 0, 0, 255) >= 4) {
        const u8 *s = phys_ptr(d->buffer);
        if (s[1] == 3 && s[0] >= 4)
            language = s[2] | ((u16)s[3] << 8);
    }
    string_descriptor(d, desc[14], language, d->info.manufacturer, sizeof(d->info.manufacturer));
    string_descriptor(d, desc[15], language, d->info.product_name, sizeof(d->info.product_name));
    string_descriptor(d, desc[16], language, d->info.serial, sizeof(d->info.serial));
    if (ecm_control && ecm_mac_string && d->ecm_in_ep && d->ecm_out_ep) {
        char value[16];
        string_descriptor(d, ecm_mac_string, language, value, sizeof(value));
        if (strlen(value) == 12) {
            bool valid = true;
            for (u32 i = 0; i < 6; ++i) {
                int a = nibble(value[i * 2]), b = nibble(value[i * 2 + 1]);
                if (a < 0 || b < 0) { valid = false; break; }
                d->ecm_mac[i] = (u8)((a << 4) | b);
            }
            if (!valid || (d->ecm_mac[0] & 1)) d->ecm_in_ep = 0;
        } else d->ecm_in_ep = 0;
    } else d->ecm_in_ep = 0;
    if (control(d, 0, 9, config_value, 0, 0) < 0)
        return -NV_EIO;
    d->info.state = NV_USB_CONFIGURED;
    if (d->info.class_code == 9)
        return configure_hub(d);
    if (d->ecm_in_ep)
        return configure_ecm(d);
    int result = configure_keyboard(d);
    return result < 0 ? result : configure_mouse(d);
}
static bool remove_device(struct device *d) {
    if (!d->slot)
        return true;
    d->dead = true; /* Completion events must not re-arm an endpoint being removed. */
    if (d == ecm_device) { net_usb_detach(); ecm_device = NULL; }
    if (d == mouse_device) { console_pointer_report(0, 0, 0); mouse_device = NULL; }
    d->repeat_key = 0;
    for (u32 i = 0; i < ARRAY_LEN(devices); ++i)
        if (devices[i].slot && devices[i].info.parent == (u32)(d - devices) + 1)
            if (!remove_device(&devices[i]))
                return false;
    if (command(d->host, 10, 0, d->slot << 24) < 0) {
        host_failed(d->host);
        return false;
    }
    ((u64 *)phys_ptr(d->host->dcbaa))[d->slot] = 0;
    u32 pages[] = {d->input, d->output, d->buffer, d->control.page,
                   d->interrupt.page, d->report_page, d->mouse_ring.page, d->mouse_page,
                   d->ecm_in.page,
                   d->ecm_out.page, d->ecm_in_buf, d->ecm_out_buf,
                   d->cdc_in.page, d->cdc_out.page, d->cdc_in_buf, d->cdc_out_buf};
    for (u32 i = 0; i < ARRAY_LEN(pages); ++i)
        if (pages[i])
            page_free(pages[i]);
    memset(d, 0, sizeof(*d));
    return true;
}
static struct device *at_port(struct host *h, u32 parent, u32 port) {
    for (u32 i = 0; i < ARRAY_LEN(devices); ++i)
        if (devices[i].slot && devices[i].host == h && devices[i].info.parent == parent &&
            devices[i].info.port == port)
            return &devices[i];
    return NULL;
}
static int attach(struct host *h, struct device *parent, u32 port, u32 speed) {
    struct device *d = NULL;
    for (u32 i = 0; i < ARRAY_LEN(devices); ++i)
        if (!devices[i].slot) {
            d = &devices[i];
            break;
        }
    if (!d)
        return -NV_ENOSPC;
    int slot = command(h, 9, 0, h->slot_type[parent ? parent->root_port - 1 : port - 1] << 16);
    if (slot <= 0 || (u32)slot > h->slots)
        return -NV_EIO;
    memset(d, 0, sizeof(*d));
    d->host = h;
    d->slot = (u32)slot;
    d->info.controller = (u32)(h - hosts);
    d->info.parent = parent ? (u32)(parent - devices) + 1 : 0;
    d->info.port = port;
    d->info.speed = speed;
    d->root_port = parent ? parent->root_port : port;
    d->depth = parent ? parent->depth + 1 : 0;
    d->route = parent ? parent->route | (port << (4 * parent->depth)) : 0;
    d->tt =
        parent ? ((speed <= 2 && parent->info.speed == 3) ? parent->slot | (port << 8) : parent->tt)
               : 0;
    d->max_packet = speed >= 4 ? 512 : speed == 3 ? 64 : 8;
    d->input = page_alloc_below(USB_DMA_LIMIT);
    d->output = page_alloc_below(USB_DMA_LIMIT);
    d->buffer = page_alloc_below(USB_DMA_LIMIT);
    int result = -NV_ENOMEM;
    if (!d->input || !d->output || !d->buffer || !ring_init(&d->control))
        goto fail;
    input_context(d, 0)[1] = 3;
    slot_context(d, 1);
    u32 *ep = input_context(d, 2);
    ep[1] = (3u << 1) | (4u << 3) | (d->max_packet << 16);
    ep[2] = d->control.page | 1;
    ep[4] = 8;
    ((u64 *)phys_ptr(h->dcbaa))[d->slot] = d->output;
    barrier();
    result = command(h, 11, d->input, d->slot << 24);
    if (result < 0)
        goto fail;
    d->info.address = ((volatile u32 *)phys_ptr(d->output))[3] & 255;
    result = identify(d);
    if (result < 0)
        goto fail;
    kprintf("[usb] %u:%u address=%u id=%x:%x %s\n", (u32)(h - hosts), port, d->info.address,
            d->info.vendor, d->info.product, d->info.product_name);
    return 0;
fail:
    kprintf("[usb] %u:%u enumeration failed (%d)\n", (u32)(h - hosts), port, result);
    remove_device(d);
    return result;
}
static u32 port_offset(struct host *h, u32 port) {
    return h->op + 0x400 + (port - 1) * 16;
}
static u32 port_preserve(u32 value) {
    return value & ((1u << 9) | (3u << 14) | (7u << 25));
}
static int reset_root_port(struct host *h, u32 port) {
    u32 off = port_offset(h, port);
    delay_ms(100); /* Attach debounce. */
    u32 state = read32(h, off);
    if (!(state & 1))
        return -NV_ENODEV;
    write32(h, off, port_preserve(state) | PORT_CHANGES | (1u << 4));
    if (!wait_bits(h, off, 1u << 4, 0, 500))
        return -NV_EIO;
    state = read32(h, off);
    write32(h, off, port_preserve(state) | PORT_CHANGES);
    delay_ms(10);
    if ((state & 3) != 3)
        return -NV_ENODEV;
    u32 speed = (state >> 10) & 15;
    return speed >= 1 && speed <= 5 ? (int)speed : -NV_EINVAL;
}
static int hub_status(struct device *d, u32 port, u32 *status) {
    if (control(d, 0xa3, 0, 0, (u16)port, 4) < 4)
        return -NV_EIO;
    memcpy(status, phys_ptr(d->buffer), 4);
    return 0;
}
static void hub_clear_changes(struct device *d, u32 port, u32 status) {
    for (u32 i = 0; i < 5; ++i)
        if (status & (1u << (16 + i)))
            control(d, 0x23, 1, (u16)(16 + i), (u16)port, 0);
}
static void scan_hub(struct device *hub) {
    if (hub->dead || hub->depth >= 5)
        return;
    for (u32 port = 1; port <= hub->hub_ports; ++port) {
        u32 state;
        if (hub_status(hub, port, &state) < 0)
            return;
        struct device *child = at_port(hub->host, (u32)(hub - devices) + 1, port);
        if (child && (!(state & 1) || (state & (1u << 16)) || child->dead)) {
            if (!remove_device(child))
                return;
            child = NULL;
        }
        if (!(state & 1) || (state & (1u << 16)))
            hub->attempted[port - 1] = 0;
        hub_clear_changes(hub, port, state);
        if (!(state & 1) || child || hub->attempted[port - 1])
            continue;
        hub->attempted[port - 1] = 1;
        delay_ms(100);
        if (control(hub, 0x23, 3, 4, (u16)port, 0) < 0)
            continue;
        u32 start = ticks;
        do {
            delay_ms(10);
            if (hub_status(hub, port, &state) < 0)
                break;
        } while ((state & (1u << 4)) && ticks - start < 50);
        hub_clear_changes(hub, port, state);
        if ((state & 3) != 3 || (state & (1u << 4)))
            continue;
        delay_ms(10);
        u32 speed = hub->info.speed >= 4   ? hub->info.speed
                    : (state & (1u << 9))  ? 2
                    : (state & (1u << 10)) ? 3
                                           : 1;
        attach(hub->host, hub, port, speed);
    }
    /* Walk children explicitly: recycled device slots need not be in tree order. */
    for (u32 i = 0; i < ARRAY_LEN(devices); ++i)
        if (devices[i].slot && devices[i].info.parent == (u32)(hub - devices) + 1 &&
            devices[i].info.state == NV_USB_HUB)
            scan_hub(&devices[i]);
}
static void scan(void) {
    for (u32 n = 0; n < host_count; ++n) {
        struct host *h = &hosts[n];
        if (h->info.state != NV_USB_RUNNING)
            continue;
        h->changed = false;
        for (u32 port = 1; port <= h->info.ports; ++port) {
            u32 off = port_offset(h, port), state = read32(h, off);
            struct device *d = at_port(h, 0, port);
            if (d && (!(state & 1) || (state & (1u << 17)) || d->dead)) {
                if (!remove_device(d))
                    break;
                d = NULL;
            }
            if (!(state & 1) || (state & (1u << 17)))
                h->attempted[port - 1] = 0;
            write32(h, off, port_preserve(state) | PORT_CHANGES | (1u << 9));
            if (!(state & 1) || d || h->attempted[port - 1])
                continue;
            h->attempted[port - 1] = 1;
            int speed = reset_root_port(h, port);
            if (speed > 0)
                attach(h, NULL, port, (u32)speed);
        }
    }
    for (u32 i = 0; i < ARRAY_LEN(devices); ++i)
        if (devices[i].slot && !devices[i].info.parent && devices[i].info.state == NV_USB_HUB)
            scan_hub(&devices[i]);
    last_scan = ticks;
}
static bool init_host(struct host *h) {
    u32 bar = pci_read(h->pci, 0x10);
    if (bar & 1)
        return false;
    u64 physical = bar & ~15u;
    if ((bar & 6) == 4)
        physical |= (u64)pci_read(h->pci, 0x14) << 32;
    else if (bar & 6)
        return false;
    if (!physical)
        return false;
    h->mmio = vm_mmio_map(physical, MMIO_SIZE);
    if (!h->mmio)
        return false;
    pci_write16(h->pci, 4, (u16)(pci_read(h->pci, 4) | 2u));
    u32 cap = read32(h, 0), hcs1 = read32(h, 4), hcs2 = read32(h, 8), hcc = read32(h, 16);
    if ((cap >> 16) < 0x0090 || (cap >> 16) > 0x0200 || (cap & 255) < 0x20)
        return false;
    h->op = cap & 255;
    h->info.ports = (hcs1 >> 24) & 255;
    h->slots = MIN(hcs1 & 255, NV_USB_DEVICE_MAX);
    h->context_size = hcc & 4 ? 64 : 32;
    h->doorbell = read32(h, 0x14) & ~3u;
    u32 runtime = read32(h, 0x18) & ~31u;
    if (runtime > MMIO_SIZE - 0x60)
        return false;
    h->runtime = runtime + 0x20;
    if (!h->slots || !h->info.ports || h->info.ports > MAX_PORTS ||
        h->op + 0x400 + h->info.ports * 16 > MMIO_SIZE ||
        h->doorbell > MMIO_SIZE - 4 * (h->slots + 1) || h->runtime > MMIO_SIZE - 0x40)
        return false;
    u32 ext = (hcc >> 16) * 4;
    for (u32 count = 0; ext && count < 256; ++count) {
        if (ext > MMIO_SIZE - 16)
            return false;
        u32 header = read32(h, ext), kind = header & 255;
        if (kind == 1) {
            write32(h, ext, header | (1u << 24));
            if (!wait_bits(h, ext, 1u << 16, 0, 1000))
                return false;
            write32(h, ext + 4, read32(h, ext + 4) & ~0xe00du);
        } else if (kind == 2) {
            u32 ports = read32(h, ext + 8), first = ports & 255, number = (ports >> 8) & 255;
            u32 slot_type = read32(h, ext + 12) & 31;
            if (!first || first > h->info.ports || number > h->info.ports - first + 1)
                return false;
            for (u32 p = first; p < first + number; ++p)
                h->slot_type[p - 1] = slot_type;
        }
        u32 next = (header >> 8) & 255;
        if (!next)
            break;
        ext += next * 4;
    }
    write32(h, h->op, read32(h, h->op) & ~1u);
    if (!wait_bits(h, h->op + 4, 1, 1, 1000))
        return false;
    write32(h, h->op, 2);
    if (!wait_bits(h, h->op, 2, 0, 1000) || !wait_bits(h, h->op + 4, 1u << 11, 0, 1000) ||
        !(read32(h, h->op + 8) & 1))
        return false;
    h->scratch_count = ((hcs2 >> 27) & 31) | (((hcs2 >> 21) & 31) << 5);
    if (h->scratch_count > MAX_SCRATCH)
        return false;
    h->dcbaa = page_alloc_below(USB_DMA_LIMIT);
    h->event_page = page_alloc_below(USB_DMA_LIMIT);
    h->erst = page_alloc_below(USB_DMA_LIMIT);
    if (!h->dcbaa || !h->event_page || !h->erst || !ring_init(&h->command))
        return false;
    if (h->scratch_count) {
        h->scratch_array = page_alloc_below(USB_DMA_LIMIT);
        if (!h->scratch_array)
            return false;
        ((u64 *)phys_ptr(h->dcbaa))[0] = h->scratch_array;
        for (u32 i = 0; i < h->scratch_count; ++i) {
            h->scratch[i] = page_alloc_below(USB_DMA_LIMIT);
            if (!h->scratch[i])
                return false;
            ((u64 *)phys_ptr(h->scratch_array))[i] = h->scratch[i];
        }
    }
    u32 *erst = phys_ptr(h->erst);
    erst[0] = h->event_page;
    erst[2] = RING_TRBS;
    h->event_cycle = 1;
    /* ERSTBA may trigger a DMA read immediately, while the controller is halted. */
    pci_write16(h->pci, 4, (u16)(pci_read(h->pci, 4) | 6u | (1u << 10)));
    barrier();
    write64(h, h->op + 0x30, h->dcbaa);
    write64(h, h->op + 0x18, h->command.page | 1);
    write32(h, h->op + 0x38, h->slots);
    write32(h, h->op + 0x14, 0);
    /* One polled interrupter: event production does not require MSI/PIC routing. */
    write32(h, h->runtime, 0);
    write32(h, h->runtime + 4, 0);
    write32(h, h->runtime + 8, 1);
    write64(h, h->runtime + 0x10, h->erst);
    write64(h, h->runtime + 0x18, h->event_page);
    pci_write16(h->pci, 4, (u16)(pci_read(h->pci, 4) | 6u | (1u << 10)));
    barrier();
    h->info.state = NV_USB_RUNNING;
    write32(h, h->op, 1);
    if (!wait_bits(h, h->op + 4, 1, 0, 1000))
        return false;
    return command(h, 23, 0, 0) >= 0;
}
static void discover_host(u32 address, u32 id, u32 cls) {
    if ((cls >> 16) != 0x0c03 || host_count == NV_USB_CONTROLLER_MAX)
        return;
    struct host *h = &hosts[host_count++];
    h->pci = address;
    h->info = (struct nv_usb_controller){(address >> 16) & 255,
                                         (address >> 11) & 31,
                                         (address >> 8) & 7,
                                         id & 0xffff,
                                         id >> 16,
                                         (cls >> 8) & 255,
                                         0,
                                         NV_USB_UNSUPPORTED};
    if (h->info.interface == 0x30 && !init_host(h)) {
        if (h->info.state == NV_USB_RUNNING)
            host_failed(h);
        else if (h->info.state != NV_USB_FAILED) {
            /* Initialization failed before any DMA pointers were published. */
            u32 pages[] = {h->dcbaa, h->event_page, h->erst, h->scratch_array, h->command.page};
            for (u32 i = 0; i < ARRAY_LEN(pages); ++i)
                if (pages[i])
                    page_free(pages[i]);
            for (u32 i = 0; i < MIN(h->scratch_count, MAX_SCRATCH); ++i)
                if (h->scratch[i])
                    page_free(h->scratch[i]);
        }
        h->info.state = NV_USB_FAILED;
    }
}
void usb_init(void) {
    busy = true;
    pci_visit(discover_host);
    initialized = true;
    scan();
    busy = false;
    kprintf("[ok] PCI USB discovery: %u controller(s)\n", host_count);
}
void usb_poll(void) {
    if (!initialized || busy || !host_count)
        return;
    busy = true;
    bool changed = false;
    for (u32 i = 0; i < host_count; ++i) {
        events(&hosts[i]);
        changed |= hosts[i].changed;
    }
    for (u32 i = 0; i < ARRAY_LEN(devices); ++i) {
        struct device *d = &devices[i];
        if (d->slot && !d->dead && d->repeat_key && (i32)(ticks - d->repeat_at) >= 0) {
            console_usb_key(d->repeat_key, d->previous[0]);
            d->repeat_at = ticks + 5;
        }
    }
    if (changed || ticks - last_scan >= 50)
        scan();
    busy = false;
}
u32 usb_pointer_count(void) {
    return mouse_device && mouse_device->slot && !mouse_device->dead &&
           mouse_device->host->info.state == NV_USB_RUNNING ? 1u : 0u;
}
int usb_rescan(void) {
    if (!initialized || busy)
        return -NV_EBUSY;
    busy = true;
    for (u32 i = 0; i < host_count; ++i)
        memset(hosts[i].attempted, 0, sizeof(hosts[i].attempted));
    for (u32 i = 0; i < ARRAY_LEN(devices); ++i)
        memset(devices[i].attempted, 0, sizeof(devices[i].attempted));
    scan();
    busy = false;
    return 0;
}
int usb_controller_info(u32 index, struct nv_usb_controller *out) {
    if (index >= NV_USB_CONTROLLER_MAX)
        return -NV_EINVAL;
    if (index >= host_count)
        return 0;
    *out = hosts[index].info;
    return 1;
}
int usb_device_info(u32 index, struct nv_usb_device *out) {
    if (index >= NV_USB_DEVICE_MAX)
        return -NV_EINVAL;
    if (!devices[index].slot || !devices[index].info.state || devices[index].dead)
        return 0;
    *out = devices[index].info;
    return 1;
}
bool usb_ecm_link(void) {
    return ecm_device && !ecm_device->dead && ecm_device->info.state == NV_USB_ETHERNET &&
           ecm_device->host->info.state == NV_USB_RUNNING;
}
int usb_ecm_send(const void *packet, u32 length) {
    if (!usb_ecm_link()) return -NV_ENODEV;
    if (length < 14 || length > 1514) return -NV_EINVAL;
    struct device *d = ecm_device;
    if (d->ecm_out_wait) return -NV_EAGAIN;
    memcpy(phys_ptr(d->ecm_out_buf), packet, length);
    bool zlp = length % d->ecm_out_packet == 0;
    u32 first = ring_put(&d->ecm_out, d->ecm_out_buf, 0, length,
                         TYPE(1) | (zlp ? 1u << 4 : 1u << 5));
    d->ecm_out_wait = zlp ? ring_put(&d->ecm_out, d->ecm_out_buf, 0, 0,
                                     TYPE(1) | (1u << 5)) : first;
    barrier();
    write32(d->host, d->host->doorbell + d->slot * 4, d->ecm_out_ep);
    return 0;
}
int usb_wifi_command(const char *command, u32 length) {
    if (!usb_ecm_link() || ecm_device->info.vendor != 0x303a ||
        !ecm_device->cdc_out_ep) return -NV_ENODEV;
    if (!length || length > 200 || command[length - 1] != '\n') return -NV_EINVAL;
    for (u32 i = 0; i + 1 < length; ++i)
        if ((u8)command[i] < 32 || (u8)command[i] > 126) return -NV_EINVAL;
    struct device *d = ecm_device;
    if (d->cdc_out_wait) return -NV_EAGAIN;
    wifi_response_size = 0;
    memcpy(phys_ptr(d->cdc_out_buf), command, length);
    d->cdc_out_wait = ring_put(&d->cdc_out, d->cdc_out_buf, 0, length,
                               TYPE(1) | (1u << 5));
    barrier();
    write32(d->host, d->host->doorbell + d->slot * 4, d->cdc_out_ep);
    return 0;
}
int usb_wifi_read(char *out, u32 capacity) {
    if (!usb_ecm_link() || ecm_device->info.vendor != 0x303a ||
        !ecm_device->cdc_in_ep) return -NV_ENODEV;
    u32 size = MIN(wifi_response_size, capacity);
    memcpy(out, wifi_response, size);
    memmove(wifi_response, wifi_response + size, wifi_response_size - size);
    wifi_response_size -= size;
    return (int)size;
}
