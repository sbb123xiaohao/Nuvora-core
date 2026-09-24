#include "kernel.h"
#include <nv/net.h>
#define QMAX 256u
#define BUFS 32u
struct vdesc { u64 address; u32 length; u16 flags, next; };
struct used_elem { u32 id, length; };
struct queue {
    struct vdesc *desc;
    volatile u16 *avail, *used;
    volatile struct used_elem *elements;
    uptr ring, buffers[BUFS];
    u32 ring_pages, size, count;
    u16 consumed, produced;
    bool busy[BUFS];
};
static struct queue rx, tx;
static u16 io;
static bool ready;
static u32 pci_address;
static u8 mac[6];
static void discover(u32 address, u32 id, u32 cls) {
    if (!io && id == 0x10001af4u && cls >> 24 == 2 && !(cls & 255u)) {
        u32 bar = pci_read(address, 0x10);
        if ((bar & 1) && (bar & ~3u) && (bar & ~3u) <= 0xffc0) {
            io = (u16)(bar & ~3u); pci_address = address;
        }
    }
}
static void dispose(struct queue *q) {
    for (u32 i = 0; i < q->count; ++i) if (q->buffers[i]) page_free(q->buffers[i]);
    for (u32 i = 0; i < q->ring_pages; ++i) page_free(q->ring + i * PAGE);
    memset(q, 0, sizeof(*q));
}
static bool create(struct queue *q, u16 index) {
    outw(io + 14, index);
    q->size = inw(io + 12);
    if (q->size < 2 || q->size > QMAX || (q->size & (q->size - 1)) || inl(io + 8)) return false;
    u32 used_offset = ALIGN_UP(16u * q->size + 2u * (3 + q->size), PAGE);
    u32 bytes = used_offset + ALIGN_UP(6 + 8u * q->size, PAGE), order = 0;
    while ((PAGE << order) < bytes) ++order;
    q->ring = page_alloc_order(order);
    if (!q->ring) return false;
    q->ring_pages = 1u << order;
    q->desc = phys_ptr(q->ring);
    q->avail = (void *)((u8 *)q->desc + 16 * q->size);
    q->used = (void *)((u8 *)q->desc + used_offset);
    q->elements = (void *)(q->used + 2);
    q->avail[0] = 1;
    q->count = MIN(q->size / 2, BUFS);
    for (u32 i = 0; i < q->count; ++i) {
        uptr p = q->buffers[i] = page_alloc();
        if (!p) return false;
        q->desc[2 * i] = (struct vdesc){p, 10, index == 0 ? 3 : 1, (u16)(2 * i + 1)};
        q->desc[2 * i + 1] = (struct vdesc){p + 16, 1514, index == 0 ? 2 : 0, 0};
    }
    __atomic_thread_fence(__ATOMIC_RELEASE);
    outl(io + 8, (u32)(q->ring / PAGE));
    return true;
}
static void offer(struct queue *q, u32 slot) {
    q->busy[slot] = true;
    q->avail[2 + q->produced % q->size] = (u16)(slot * 2);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    q->avail[1] = ++q->produced;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}
bool virtio_net_init(u8 *out_mac) {
    pci_visit(discover);
    if (!io) return false;
    u16 command = (u16)pci_read(pci_address, 4);
    pci_write16(pci_address, 4, command | 5u | 0x400u);
    outb(io + 18, 0);
    if (inb(io + 18)) return false;
    outb(io + 18, 1); outb(io + 18, 3);
    u32 features = inl(io);
    if (!(features & (1u << 5))) goto fail;
    /* Legacy transport, no checksum/GSO/merged-buffer offload. A separate
     * ten-byte header descriptor preserves legacy framing requirements. */
    outl(io + 4, 1u << 5);
    for (u32 i = 0; i < 6; ++i) mac[i] = inb(io + 20 + i);
    if ((mac[0] & 1) || !memcmp(mac, "\0\0\0\0\0\0", 6)) goto fail;
    if (!create(&rx, 0) || !create(&tx, 1)) goto fail;
    for (u32 i = 0; i < rx.count; ++i) offer(&rx, i);
    outb(io + 18, 7);
    if (inb(io + 18) != 7) goto fail;
    outw(io + 16, 0); memcpy(out_mac, mac, 6); ready = true;
    return true;
fail:
    outb(io + 18, 0);
    if (!inb(io + 18)) { dispose(&rx); dispose(&tx); }
    /* Unresponsive DMA buffers are quarantined rather than recycled. */
    pci_write16(pci_address, 4, command & ~4u);
    return false;
}
bool virtio_net_ready(void) { return ready; }
static bool completed(struct queue *q, u32 *slot, u32 *length) {
    u16 end = q->used[1];
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (end == q->consumed) return false;
    if ((u16)(end - q->consumed) > q->count) goto corrupt;
    struct used_elem item = q->elements[q->consumed % q->size];
    if ((item.id & 1) || item.id >= 2 * q->count || !q->busy[item.id / 2]) goto corrupt;
    *slot = item.id / 2; *length = item.length;
    q->busy[*slot] = false; ++q->consumed; return true;
corrupt:
    ready = false; outb(io + 18, 0);
    kprintf("[error] virtio-net: invalid used ring; device stopped\n");
    return false;
}
int virtio_net_send(const u8 *data, u32 length) {
    if (!ready) return -NV_ENODEV;
    if (length < 14 || length > 1514) return -NV_EINVAL;
    u32 slot, size;
    for (u32 n = 0; n < tx.count && completed(&tx, &slot, &size); ++n) { }
    if (!ready) return -NV_EIO;
    for (u32 i = 0; i < tx.count; ++i) if (!tx.busy[i]) {
        u8 *buffer = phys_ptr(tx.buffers[i]); memset(buffer, 0, 16);
        memcpy(buffer + 16, data, length); tx.desc[2 * i + 1].length = length;
        offer(&tx, i); outw(io + 16, 1); return 0;
    }
    return -NV_EAGAIN;
}
void virtio_net_poll(void (*receive)(const u8 *, u32)) {
    if (!ready) return;
    (void)inb(io + 19);
    u32 slot, length;
    for (u32 n = 0; n < rx.count && completed(&rx, &slot, &length); ++n) {
        u8 *p = phys_ptr(rx.buffers[slot]);
        if (length >= 24 && length <= 1524 && !p[0] && !p[1]) receive(p + 16, length - 10);
        memset(p, 0, 16); offer(&rx, slot);
    }
    if (ready) outw(io + 16, 0);
}
