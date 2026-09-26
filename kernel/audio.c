#include "kernel.h"

/* Intel High Definition Audio: one analog PCM output. This deliberately uses
 * the optional immediate-command interface: machines without it report no
 * output. A codec route is discovered from pin -> DAC, not assumed from a
 * particular vendor/node ID. Registers follow HDA specification rev 1.0a. */
#define HDA_WAIT 12000000u
#define HDA_DMA_LIMIT 0x100000000ull
#define HDA_SILENCE 128u
struct hda_bdl { u64 address; u32 length, flags; };
struct hda_route { u8 node, choice; };
static struct {
    volatile u8 *regs;
    u32 pci, stream;
    uptr data_page, bdl_page;
    u8 codec, dac, pin, group;
    bool ready;
} hda;

#ifndef HDA_READ8
#define HDA_READ8(offset) (*(volatile u8 *)(hda.regs + (offset)))
#define HDA_READ16(offset) (*(volatile u16 *)(hda.regs + (offset)))
#define HDA_READ32(offset) (*(volatile u32 *)(hda.regs + (offset)))
#define HDA_WRITE8(offset, value) (*(volatile u8 *)(hda.regs + (offset)) = (value))
#define HDA_WRITE16(offset, value) (*(volatile u16 *)(hda.regs + (offset)) = (value))
#define HDA_WRITE32(offset, value) (*(volatile u32 *)(hda.regs + (offset)) = (value))
#endif
static u8 h8(u32 o) { return HDA_READ8(o); }
static u16 h16(u32 o) { return HDA_READ16(o); }
static u32 h32(u32 o) { return HDA_READ32(o); }
static void w8(u32 o, u8 x) { HDA_WRITE8(o, x); }
static void w16(u32 o, u16 x) { HDA_WRITE16(o, x); }
static void w32(u32 o, u32 x) { HDA_WRITE32(o, x); }

static bool wait_mask(u32 offset, u32 mask, u32 expected) {
    for (u32 i = 0; i < HDA_WAIT; ++i) {
        if ((h32(offset) & mask) == expected) return true;
        __asm__ volatile("pause");
    }
    return false;
}
/* Return false on a missing immediate interface, no response or timeout;
 * a legitimate all-zero codec response remains distinguishable. */
static bool verb(u8 codec, u8 node, u32 command, u32 *response) {
    if (!wait_mask(0x68, 1, 0)) return false;
    w16(0x68, 2); /* clear the previous response-valid bit */
    w32(0x60, (u32)codec << 28 | (u32)node << 20 | command);
    w16(0x68, 1);
    for (u32 i = 0; i < HDA_WAIT; ++i) {
        if ((h16(0x68) & 3) == 2) {
            *response = h32(0x64);
            w16(0x68, 2);
            return true;
        }
        __asm__ volatile("pause");
    }
    return false;
}
static bool get_param(u8 c, u8 n, u8 param, u32 *v) {
    return verb(c, n, 0xf0000u | param, v);
}
/* Connection lists can contain range-encoded entries. The initial driver
 * rejects those rather than silently choosing the wrong amplifier/DAC. */
static bool connection(u8 c, u8 n, u8 index, u8 *id) {
    u32 count, item;
    if (!get_param(c, n, 0x0e, &count) || !(count & 0x7f) ||
        index >= (count & 0x7f) || (count & 0x80)) return false;
    if (!verb(c, n, 0xf0200u | (index & ~3u), &item)) return false;
    u8 entry = (u8)(item >> ((index & 3u) * 8));
    if (!entry || (entry & 0x80)) return false;
    *id = entry;
    return true;
}
static bool find_dac(u8 c, u8 n, u8 first, u16 end, u32 depth,
                     struct hda_route *path, u8 *length) {
    if (n < first || n >= end || depth >= 5) return false;
    for (u32 i = 0; i < depth; ++i) if (path[i].node == n) return false;
    u32 caps;
    if (!get_param(c, n, 9, &caps) || (caps & (1u << 9))) return false;
    u32 type = (caps >> 20) & 15u;
    if (type == 0) {
        u32 pcm, formats;
        if (!get_param(c, n, 0x0a, &pcm) || !get_param(c, n, 0x0b, &formats) ||
            !(pcm & (1u << 6)) || !(pcm & (1u << 17)) || !(formats & 1u)) return false;
        path[depth] = (struct hda_route){n, 0};
        *length = (u8)(depth + 1);
        return true;
    }
    if (type != 2 && type != 3 && type != 4) return false;
    u32 links;
    if (!get_param(c, n, 0x0e, &links) || !(links & 0x7f) || (links & 0x80))
        return false;
    /* A short list may have more entries than fit in one response. */
    for (u32 i = 0; i < (links & 0x7f); ++i) {
        u8 child;
        if (!connection(c, n, (u8)i, &child)) continue;
        path[depth] = (struct hda_route){n, (u8)i};
        if (find_dac(c, child, first, end, depth + 1, path, length)) return true;
    }
    return false;
}
static void unmute(u8 c, u8 node, u32 caps, bool input, u8 index) {
    u32 gain_caps;
    if (!(caps & (input ? 2u : 4u)) ||
        !get_param(c, node, input ? 0x0d : 0x12, &gain_caps)) return;
    /* Offset is the gain corresponding to 0 dB. Unmute both channels. */
    u32 value = (input ? 0x4000u | (u32)index << 8 : 0x8000u) |
                0x3000u | ((gain_caps >> 16) & 0x7fu);
    u32 ignored;
    (void)verb(c, node, 0x30000u | value, &ignored);
}
static bool route_codec(u8 c) {
    u32 root, group, nodes;
    if (!get_param(c, 0, 4, &root)) return false;
    for (u32 g = (root >> 16) & 0xff, limit = g + (root & 0xff);
         g < limit && g < 256; ++g) {
        if (!get_param(c, (u8)g, 5, &group) || (group & 0xff) != 1 ||
            !get_param(c, (u8)g, 4, &nodes)) continue;
        u32 first = (nodes >> 16) & 0xff, end = first + (nodes & 0xff);
        for (u32 n = first; n < end && n < 256; ++n) {
            u32 caps, pin_caps, config;
            if (!get_param(c, (u8)n, 9, &caps) || ((caps >> 20) & 15) != 4 ||
                (caps & (1u << 9)) || !get_param(c, (u8)n, 0x0c, &pin_caps) ||
                !(pin_caps & (1u << 4)) ||
                !verb(c, (u8)n, 0xf1c00u, &config)) continue;
            u32 device = (config >> 20) & 15u;
            if ((config >> 30) == 1u || device > 2) continue;
            struct hda_route path[5] = {{0}};
            u8 length = 0;
            if (!find_dac(c, (u8)n, (u8)first, (u16)end, 0, path, &length))
                continue;
            u32 ignored;
            if (!verb(c, (u8)g, 0x70500, &ignored)) return false;
            for (u32 i = 0; i < length; ++i) {
                u32 wc;
                if (!get_param(c, path[i].node, 9, &wc)) return false;
                if (wc & (1u << 10))
                    if (!verb(c, path[i].node, 0x70500, &ignored)) return false;
                if (i + 1 < length) {
                    if (!verb(c, path[i].node, 0x70100u | path[i].choice, &ignored))
                        return false;
                    unmute(c, path[i].node, wc, true, path[i].choice);
                }
                unmute(c, path[i].node, wc, false, 0);
            }
            if (!verb(c, (u8)n, 0x70700u | (device == 2 ? 0xc0u : 0x40u),
                      &ignored)) return false;
            if ((pin_caps & (1u << 16)) && !verb(c, (u8)n, 0x70c02u, &ignored))
                return false;
            u8 dac = path[length - 1].node;
            /* 48 kHz, 16 bits, two channels: FMT 0x0011. */
            if (!verb(c, dac, 0x20011u, &ignored) ||
                !verb(c, dac, 0x70610u, &ignored)) return false;
            hda.codec = c; hda.group = (u8)g; hda.pin = (u8)n; hda.dac = dac;
            return true;
        }
    }
    return false;
}
static void discover(u32 address, u32 id, u32 class_code) {
    (void)id;
    if (hda.regs || (class_code >> 8) != 0x040300u) return;
    u32 bar = pci_read(address, 0x10);
    if (bar & 1u || ((bar >> 1) & 3u) == 1 || ((bar >> 1) & 3u) == 3) return;
    u64 physical = bar & ~15u;
    if ((bar & 6u) == 4) physical |= (u64)pci_read(address, 0x14) << 32;
    if (!physical || physical % PAGE || physical >> 52) return;
    hda.regs = vm_mmio_map(physical, PAGE);
    if (hda.regs) hda.pci = address;
}
static bool stream_reset(void) {
    u32 o = hda.stream;
    w8(o, 0); /* RUN=0 */
    for (u32 i = 0; i < HDA_WAIT; ++i) if (!(h8(o) & 2)) break;
    if (h8(o) & 2) return false;
    w8(o, 1); /* SRST=1, wait for hardware acknowledge */
    for (u32 i = 0; i < HDA_WAIT; ++i) if (h8(o) & 1) break;
    if (!(h8(o) & 1)) return false;
    w8(o, 0);
    for (u32 i = 0; i < HDA_WAIT; ++i) if (!(h8(o) & 1)) return true;
    return false;
}
void audio_init(void) {
    memset(&hda, 0, sizeof(hda));
    pci_visit(discover);
    if (!hda.regs) { kprintf("[audio] no HDA controller\n"); return; }
    u16 gcap = h16(0);
    u32 inputs = (gcap >> 8) & 15u, outputs = (gcap >> 12) & 15u;
    hda.stream = 0x80 + inputs * 0x20;
    if (!outputs || hda.stream + 0x20 > PAGE) goto unavailable;
    pci_write16(hda.pci, 4, (u16)(pci_read(hda.pci, 4) | 6u));
    w32(0x08, h32(0x08) & ~1u);
    if (!wait_mask(0x08, 1, 0)) goto unavailable;
    w32(0x08, h32(0x08) | 1u);
    if (!wait_mask(0x08, 1, 1)) goto unavailable;
    w8(0x4c, 0); w8(0x5c, 0); /* stop firmware CORB/RIRB for immediate verbs */
    u16 present = h16(0x0e);
    bool found = false;
    for (u32 c = 0; c < 15 && !found; ++c)
        if (present & (1u << c)) found = route_codec((u8)c);
    if (!found) goto unavailable;
    hda.data_page = page_alloc_below(HDA_DMA_LIMIT);
    hda.bdl_page = page_alloc_below(HDA_DMA_LIMIT);
    if (!hda.data_page || !hda.bdl_page || !stream_reset()) goto unavailable;
    hda.ready = true;
    kprintf("[audio] HDA analog output: codec %u, pin %u, DAC %u\n",
            (u32)hda.codec, (u32)hda.pin, (u32)hda.dac);
    return;
unavailable:
    /* Pages remain owned if firmware DMA may still reference them. */
    kprintf("[audio] HDA output unavailable (codec route or controller)\n");
}

static int play(const void *samples, u32 bytes) {
    u32 o = hda.stream;
    if (!stream_reset()) { hda.ready = false; return -NV_EIO; }
    u8 *data = phys_ptr(hda.data_page);
    struct hda_bdl *bdl = phys_ptr(hda.bdl_page);
    memcpy(data, samples, bytes);
    memset(data + bytes, 0, HDA_SILENCE);
    bdl[0] = (struct hda_bdl){hda.data_page, bytes, 0};
    /* Drain a short silent tail before stopping the stream. Completion is
     * latched on the second BDL entry, including when LPIB wraps to zero. */
    bdl[1] = (struct hda_bdl){hda.data_page + bytes, HDA_SILENCE, 1};
    __atomic_thread_fence(__ATOMIC_RELEASE);
    w8(o + 3, 0x1c); /* clear stale completion/error bits */
    w32(o + 8, bytes + HDA_SILENCE); /* circular buffer length */
    w16(o + 0x0c, 1); /* two BDL entries */
    w16(o + 0x12, 0x11);
    w32(o + 0x18, (u32)hda.bdl_page);
    w32(o + 0x1c, (u32)(hda.bdl_page >> 32));
    w8(o + 2, 0x10); /* stream tag 1 */
    w8(o, 2); /* RUN */
    bool complete = false;
    /* Interrupt gates enter the syscall with IF=0. Let PIT/keyboard IRQs run
     * while PCM drains; a timer IRQ in ring 0 does not reschedule this task. */
    irq_enable();
    for (u32 i = 0; i < HDA_WAIT; ++i) {
        u8 status = h8(o + 3);
        if (status & 0x18) break;
        if (status & 4) { complete = true; break; }
        __asm__ volatile("pause");
    }
    irq_disable();
    w8(o, 0);
    for (u32 i = 0; i < HDA_WAIT; ++i) if (!(h8(o) & 2)) break;
    if (h8(o) & 2) hda.ready = false; /* do not reuse a page still under DMA */
    w8(o + 3, 0x1c);
    return complete && hda.ready ? (int)bytes : -NV_EIO;
}
int audio_ioctl(u32 op, u32 pointer) {
    if (op != NV_AUDIO_INFO && op != NV_AUDIO_WRITE) return -NV_EINVAL;
    if (op == NV_AUDIO_INFO) {
        if (!user_range(current->pd, pointer, sizeof(struct nv_audio_info), true))
            return -NV_EFAULT;
        struct nv_audio_info info = {NV_AUDIO_API_VERSION, hda.ready ? 1u : 0u,
                                     48000, 2, NV_AUDIO_S16LE, NV_AUDIO_MAX_WRITE};
        memcpy((void *)(uptr)pointer, &info, sizeof(info));
        return 0;
    }
    if (!user_range(current->pd, pointer, sizeof(struct nv_audio_write), false))
        return -NV_EFAULT;
    struct nv_audio_write request;
    memcpy(&request, (const void *)(uptr)pointer, sizeof(request));
    if (!request.bytes || request.bytes > NV_AUDIO_MAX_WRITE || request.bytes % 4)
        return -NV_EINVAL;
    if (!user_range(current->pd, request.pixels, request.bytes, false))
        return -NV_EFAULT;
    if (!hda.ready) return -NV_ENODEV;
    return play((const void *)(uptr)request.pixels, request.bytes);
}
