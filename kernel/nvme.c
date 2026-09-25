#include "kernel.h"
/* NVMe PCI transport, one controller and one 512-byte NVM namespace. Poll one
 * command at a time; data crosses a private DMA page, never an arbitrary
 * kernel/user address. See NVM Express 1.0e, sections 3, 4, 5 and 6. */
#define NVME_DMA_LIMIT 0x100000000ull
#define NVME_DEPTH 16u
#define NVME_POLLS 50000000u
struct nvme_command {
    u32 cdw0, nsid;
    u64 reserved, metadata, prp1, prp2;
    u32 cdw10, cdw11, cdw12, cdw13, cdw14, cdw15;
};
struct nvme_completion {
    u32 result, reserved;
    u16 sq_head, sq_id, cid, status;
};
_Static_assert(sizeof(struct nvme_command) == 64, "NVMe submission entry");
_Static_assert(sizeof(struct nvme_completion) == 16, "NVMe completion entry");
struct nvme_queue {
    uptr sq_page, cq_page;
    u16 tail, head, next_cid, depth;
    u8 phase;
};
static struct {
    volatile u8 *regs;
    u32 pci, nsid, stride;
    u64 sectors;
    uptr data_page;
    struct nvme_queue admin, io;
    bool online;
} nvme;

#ifndef NVME_REG_READ
#define NVME_REG_READ(off) (*(volatile u32 *)(nvme.regs + (off)))
#define NVME_REG_WRITE(off, value) (*(volatile u32 *)(nvme.regs + (off)) = (value))
#endif
static u32 nr32(u32 off) { return NVME_REG_READ(off); }
static void nw32(u32 off, u32 value) { NVME_REG_WRITE(off, value); }
static u64 nr64(u32 off) { return (u64)nr32(off) | (u64)nr32(off + 4) << 32; }
static u64 le64_at(const u8 *p) { u64 n; memcpy(&n, p, 8); return n; }

static bool nvme_wait_ready(bool ready, u32 timeout) {
    u64 polls = (u64)MIN(MAX(timeout, 1u), 8u) * NVME_POLLS;
    while (polls--) {
        u32 status = nr32(0x1c);
        if (!!(status & 1u) == ready) return true;
        if (ready && (status & 2u)) return false;
        __asm__ volatile("pause");
    }
    return false;
}

static void nvme_release(bool stop) {
    if (stop && nvme.regs) {
        nw32(0x14, 0);
        /* A controller which fails to quiesce may still DMA. Leave its pages
         * pinned in that case, and disable PCI bus mastering. */
        if (!nvme_wait_ready(false, 1)) {
            pci_write16(nvme.pci, 4, (u16)(pci_read(nvme.pci, 4) & ~4u));
            nvme.online = false;
            return;
        }
        pci_write16(nvme.pci, 4, (u16)(pci_read(nvme.pci, 4) & ~4u));
    }
    uptr pages[] = {nvme.admin.sq_page, nvme.admin.cq_page,
                    nvme.io.sq_page, nvme.io.cq_page, nvme.data_page};
    for (u32 i = 0; i < ARRAY_LEN(pages); ++i)
        if (pages[i]) page_free(pages[i]);
    memset(&nvme, 0, sizeof(nvme));
}

static int nvme_submit(struct nvme_queue *queue, u32 qid,
                       const struct nvme_command *command, u32 *result) {
    if (!queue->sq_page || !queue->cq_page) return -NV_ENODEV;
    u16 cid = ++queue->next_cid;
    struct nvme_command cmd = *command;
    cmd.cdw0 = (cmd.cdw0 & 0xffffu) | (u32)cid << 16;
    struct nvme_command *sq = phys_ptr(queue->sq_page);
    volatile struct nvme_completion *cq = phys_ptr(queue->cq_page);
    sq[queue->tail] = cmd;
    queue->tail = (queue->tail + 1) % queue->depth;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    nw32(0x1000 + 2u * qid * nvme.stride, queue->tail);
    for (u32 poll = 0; poll < NVME_POLLS; ++poll) {
        u16 status = cq[queue->head].status;
        if ((status & 1u) == queue->phase) {
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            struct nvme_completion done = cq[queue->head];
            queue->head = (queue->head + 1) % queue->depth;
            if (!queue->head) queue->phase ^= 1;
            nw32(0x1000 + (2u * qid + 1u) * nvme.stride, queue->head);
            if (done.cid != cid || done.sq_id != qid || (done.status & ~1u))
                return -NV_EIO;
            if (result) *result = done.result;
            return 0;
        }
        if (nr32(0x1c) & 2u) return -NV_EIO;
        __asm__ volatile("pause");
    }
    return -NV_EIO;
}

/* FLBAS selects one of the 4-byte LBA format descriptors at offset 128.
 * The snapshot/GPT stack currently expects exactly 512-byte, no-metadata
 * sectors; a 4K-native or protected namespace must not be misinterpreted. */
static bool nvme_namespace(const u8 *id, u64 *size) {
    u32 format = (id[26] & 15u) | ((id[26] & 0x60u) >> 1);
    if (format > id[25] || format >= 64 || (id[29] & 7u)) return false;
    const u8 *lbaf = id + 128 + format * 4;
    u16 metadata;
    memcpy(&metadata, lbaf, sizeof(metadata));
    u64 blocks = le64_at(id), capacity = le64_at(id + 8);
    if (metadata || lbaf[2] != 9 || capacity < 8192 || blocks < capacity)
        return false;
    *size = blocks;
    return true;
}

static void nvme_discover(u32 address, u32 id, u32 class_code) {
    (void)id;
    if (nvme.regs || (class_code >> 8) != 0x010802u) return;
    u32 bar = pci_read(address, 0x10);
    if ((bar & 1u) || ((bar & 6u) != 0 && (bar & 6u) != 4u)) return;
    u64 physical = bar & ~15u;
    if ((bar & 6u) == 4u) physical |= (u64)pci_read(address, 0x14) << 32;
    if (!physical || (physical & (PAGE - 1)) || physical >> 52) return;
    nvme.regs = vm_mmio_map(physical, 2 * PAGE);
    if (nvme.regs) nvme.pci = address;
}

bool nvme_init(u64 *capacity) {
    memset(&nvme, 0, sizeof(nvme));
    pci_visit(nvme_discover);
    if (!nvme.regs) return false;
    u64 cap = nr64(0);
    u32 max_entries = (u32)(cap & 0xffffu) + 1u;
    u32 timeout = (u32)(cap >> 24) & 255u;
    u32 stride_shift = (u32)(cap >> 32) & 15u;
    u32 css = (u32)(cap >> 37) & 255u;
    /* Four doorbells (admin and one I/O pair) must fit into the mapped BAR. */
    if (max_entries < 2 || ((cap >> 48) & 15u) || stride_shift > 8 ||
        (css && !(css & 1u))) return false;
    nvme.stride = 4u << stride_shift;
    nvme.admin.depth = nvme.io.depth = (u16)MIN(max_entries, NVME_DEPTH);
    nvme.admin.phase = nvme.io.phase = 1;
    nvme.admin.sq_page = page_alloc_below(NVME_DMA_LIMIT);
    nvme.admin.cq_page = page_alloc_below(NVME_DMA_LIMIT);
    nvme.io.sq_page = page_alloc_below(NVME_DMA_LIMIT);
    nvme.io.cq_page = page_alloc_below(NVME_DMA_LIMIT);
    nvme.data_page = page_alloc_below(NVME_DMA_LIMIT);
    if (!nvme.admin.sq_page || !nvme.admin.cq_page || !nvme.io.sq_page ||
        !nvme.io.cq_page || !nvme.data_page) { nvme_release(false); return false; }
    /* The queues are available before bus mastering. Reset any firmware queue
     * and wait until the controller has stopped accessing its old pages. */
    pci_write16(nvme.pci, 4, (u16)(pci_read(nvme.pci, 4) | 6u));
    nw32(0x14, 0);
    if (!nvme_wait_ready(false, timeout)) { nvme_release(true); return false; }
    nw32(0x24, (u32)(nvme.admin.depth - 1) | (u32)(nvme.admin.depth - 1) << 16);
    nw32(0x28, (u32)nvme.admin.sq_page);
    nw32(0x2c, (u32)(nvme.admin.sq_page >> 32));
    nw32(0x30, (u32)nvme.admin.cq_page);
    nw32(0x34, (u32)(nvme.admin.cq_page >> 32));
    nw32(0x14, 1u | 6u << 16 | 4u << 20); /* EN, IOSQES=64, IOCQES=16. */
    if (!nvme_wait_ready(true, timeout)) { nvme_release(true); return false; }
    struct nvme_command command = { .cdw0 = 0x09, .cdw10 = 7, .cdw11 = 0 };
    if (nvme_submit(&nvme.admin, 0, &command, NULL) < 0) {
        nvme_release(true); return false;
    }
    command = (struct nvme_command){.cdw0 = 0x05, .prp1 = nvme.io.cq_page,
        .cdw10 = ((u32)nvme.io.depth - 1) << 16 | 1u, .cdw11 = 1};
    if (nvme_submit(&nvme.admin, 0, &command, NULL) < 0) {
        nvme_release(true); return false;
    }
    command = (struct nvme_command){.cdw0 = 0x01, .prp1 = nvme.io.sq_page,
        .cdw10 = ((u32)nvme.io.depth - 1) << 16 | 1u, .cdw11 = 1u << 16 | 1u};
    if (nvme_submit(&nvme.admin, 0, &command, NULL) < 0) {
        nvme_release(true); return false;
    }
    command = (struct nvme_command){.cdw0 = 0x06, .prp1 = nvme.data_page, .cdw10 = 1};
    if (nvme_submit(&nvme.admin, 0, &command, NULL) < 0) {
        nvme_release(true); return false;
    }
    u8 *id = phys_ptr(nvme.data_page);
    u32 namespaces;
    memcpy(&namespaces, id + 516, sizeof(namespaces));
    namespaces = MIN(namespaces, 16u);
    for (u32 nsid = 1; nsid <= namespaces; ++nsid) {
        memset(id, 0, PAGE);
        command = (struct nvme_command){.cdw0 = 0x06, .nsid = nsid,
                                        .prp1 = nvme.data_page};
        if (nvme_submit(&nvme.admin, 0, &command, NULL) < 0) continue;
        if (!nvme_namespace(id, &nvme.sectors)) continue;
        nvme.nsid = nsid;
        nvme.online = true;
        *capacity = nvme.sectors;
        return true;
    }
    nvme_release(true);
    return false;
}

static int nvme_transfer(u64 lba, void *buffer, bool write) {
    if (!nvme.online || lba >= nvme.sectors) return -NV_ENODEV;
    void *bounce = phys_ptr(nvme.data_page);
    if (write) memcpy(bounce, buffer, 512);
    struct nvme_command command = {.cdw0 = write ? 0x01 : 0x02,
        .nsid = nvme.nsid, .prp1 = nvme.data_page,
        .cdw10 = (u32)lba, .cdw11 = (u32)(lba >> 32)};
    int r = nvme_submit(&nvme.io, 1, &command, NULL);
    if (r < 0) { nvme.online = false; return r; }
    if (!write) memcpy(buffer, bounce, 512);
    return 0;
}
int nvme_read(u64 lba, void *out) { return nvme_transfer(lba, out, false); }
int nvme_write(u64 lba, const void *data) { return nvme_transfer(lba, (void *)data, true); }
int nvme_flush(void) {
    if (!nvme.online) return -NV_ENODEV;
    struct nvme_command command = {.nsid = nvme.nsid}; /* opcode 0 = Flush */
    int r = nvme_submit(&nvme.io, 1, &command, NULL);
    if (r < 0) nvme.online = false;
    return r;
}
bool nvme_ready(void) { return nvme.online; }
void nvme_shutdown(void) { nvme_release(true); }
