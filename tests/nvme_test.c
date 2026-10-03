/* Run the actual NVMe queue/GPT/disk code against a doorbell and DMA model.
 * The disk image is the same generated dual-partition GPT fixture as ATA. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <nv/abi.h>
#include <nv/string.h>
#define NV_KERNEL_H
#define PAGE 4096u
#define SNAP_CAP_MAX (16u * 1024u * 1024u)
static volatile u64 ticks;
static bool irq_enabled;
static bool stale_timer_irq, imminent_timer_edge;
static u64 elapsed_us;
static uptr irq_save(void) { uptr flags = irq_enabled ? 0x200u : 0; irq_enabled = false; return flags; }
static void irq_restore(uptr flags) { irq_enabled = !!(flags & 0x200u); }
static void idle_once(void) {
    assert(!irq_enabled); ++ticks;
    if (stale_timer_irq) stale_timer_irq = false;
    else if (imminent_timer_edge) { imminent_timer_edge = false; ++elapsed_us; }
    else elapsed_us += 10000;
}
struct store_layout { u32 slot_lba[2], slot_sectors, snap_cap; u32 version; u64 data_first, data_end; };
static FILE *disk_file;
static u64 disk_sectors;
static u8 dma[32][PAGE];
static u32 next_page, pci_command[2], completed, flushes, writes;
static bool multi_namespace, multi_controller, sparse_namespace, legacy_list;
static bool fail_io;
enum admin_fault { NO_FAULT, LIST_TIMEOUT, NAMESPACE_TIMEOUT, BAD_COMPLETION, FATAL_STATUS, FATAL_COMPLETION };
static enum admin_fault admin_fault;
static bool fault_fired, admin_pending, stop_blocked, invalid_namespace, late_posted;
static bool page_live[32];
static u32 freed, unsafe_submits, unsafe_dma_reuse, late_consumed;
static uptr pending_prp;
static u16 pending_cid;
static u64 cap = 15u | 1ull << 37;
static u32 ready;
static bool ready_pending;
static u32 ready_target, ready_delay, controller_ready_delay;
static u64 ready_at, status_reads;
static uptr page_alloc_below(u64 limit) {
    assert(limit >= 0x100000000ull && next_page + 1 < 32);
    ++next_page; assert(!page_live[next_page]); page_live[next_page] = true;
    memset(dma[next_page], 0, PAGE);
    return next_page * PAGE;
}
static void page_free(uptr page) {
    assert(page / PAGE <= next_page && page_live[page / PAGE]);
    assert(!admin_pending); /* The device must relinquish every DMA page. */
    page_live[page / PAGE] = false; ++freed;
}
static void *phys_ptr(uptr physical) {
    assert(physical && physical % PAGE == 0 && physical / PAGE <= next_page);
    return dma[physical / PAGE];
}
static void *vm_mmio_map(u64 physical, u32 size) {
    assert((physical == 0x40000000u ||
            (multi_controller && physical == 0x50000000u)) && size == 2 * PAGE);
    return dma[0];
}
static u32 pci_read(u32 address, u32 offset) {
    assert(address == 0x1000 || (multi_controller && address == 0x2000));
    return offset == 0x10 ? (address == 0x2000 ? 0x50000000u : 0x40000000u) :
           offset == 4 ? pci_command[address == 0x2000] : 0;
}
static void pci_write16(u32 address, u32 offset, u16 value) {
    assert((address == 0x1000 || (multi_controller && address == 0x2000)) && offset == 4);
    pci_command[address == 0x2000] = value;
}
static void pci_visit(void (*visit)(u32, u32, u32)) {
    visit(0x1000, 0x12348086, 0x01080200);
    if (multi_controller) visit(0x2000, 0x12348086, 0x01080200);
}
static u8 inb(u16 port) { (void)port; return 0xff; }
static u16 inw(u16 port) { (void)port; return 0; }
static void outb(u16 port, u8 value) { (void)port; (void)value; }
static void outw(u16 port, u16 value) { (void)port; (void)value; }
static bool ahci_init(u64 *capacity, u32 first_pci, u32 first_port,
                      u32 *selected_pci, u32 *selected_port) {
    (void)capacity; (void)first_pci; (void)first_port;
    (void)selected_pci; (void)selected_port; return false;
}
static int ahci_read(u64 lba, void *out) { (void)lba; (void)out; return -NV_ENODEV; }
static int ahci_write(u64 lba, const void *in) { (void)lba; (void)in; return -NV_ENODEV; }
static int ahci_flush(void) { return -NV_ENODEV; }
static bool ahci_ready(void) { return false; }
static bool ahci_shutdown(void) { return true; }
static u32 sim_read(u32 offset) {
    if (offset == 0) return (u32)cap;
    if (offset == 4) return (u32)(cap >> 32);
    if (offset == 0x1c) {
        ++status_reads;
        if (ready_pending && ticks - ready_at >= ready_delay) {
            ready = ready_target; ready_pending = false;
        }
        return ready;
    }
    return 0;
}
static void sim_write(u32 offset, u32 value);
#define NVME_REG_READ(off) sim_read(off)
#define NVME_REG_WRITE(off, value) sim_write(off, value)
#include "../kernel/nvme.c"
#include "../kernel/disk.c"

static void sim_write(u32 offset, u32 value) {
    if (offset == 0x14) {
        if (!value && admin_pending) {
            const u8 *bytes = phys_ptr(pending_prp);
            for (u32 i = 0; i < PAGE; ++i)
                if (bytes[i] != 0xa5) { ++unsafe_dma_reuse; break; }
            if (stop_blocked) return;
            admin_pending = false;
        }
        if (controller_ready_delay) {
            ready_target = !!(value & 1); ready_delay = controller_ready_delay;
            ready_at = ticks; ready_pending = true; return;
        }
        ready = !!(value & 1); return;
    }
    if (offset == 0x1004 && late_posted) ++late_consumed;
    if (offset != 0x1000 && offset != 0x1008) return;
    u32 qid = offset == 0x1008;
    struct nvme_queue *q = qid ? &nvme.io : &nvme.admin;
    struct nvme_command *sq = phys_ptr(q->sq_page);
    struct nvme_command *cmd = &sq[(value + q->depth - 1) % q->depth];
    struct nvme_completion *cq = phys_ptr(q->cq_page);
    if (admin_pending) {
        /* If a driver wrongly submits again, the old CQE can arrive late.
         * It belongs to the pending command, never to this new submission. */
        ++unsafe_submits;
        late_posted = true;
        cq[q->head] = (struct nvme_completion){.cid=pending_cid, .sq_id=0, .status=q->phase};
        return;
    }
    struct nvme_completion done = {.cid = (u16)(cmd->cdw0 >> 16),
                                   .sq_id = (u16)qid, .status = q->phase};
    if (!qid && (cmd->cdw0 & 255u) == 6 && !fault_fired &&
        ((admin_fault == LIST_TIMEOUT && cmd->cdw10 == 2) ||
         (admin_fault != NO_FAULT && admin_fault != LIST_TIMEOUT && !cmd->cdw10))) {
        fault_fired = admin_pending = true;
        pending_prp = (uptr)cmd->prp1; pending_cid = done.cid;
        memset(phys_ptr(pending_prp), 0xa5, PAGE);
        if (admin_fault == BAD_COMPLETION) { done.cid ^= 1; cq[q->head] = done; }
        if (admin_fault == FATAL_STATUS) ready |= 2;
        if (admin_fault == FATAL_COMPLETION) {
            ready |= 2; done.status |= 2u; cq[q->head] = done;
        }
        return;
    }
    if (!qid && (cmd->cdw0 & 255u) == 6) {
        u8 *id = phys_ptr((uptr)cmd->prp1);
        if (cmd->cdw10 == 1) {
            u32 n = (multi_namespace || sparse_namespace) && nvme.pci == 0x1000 ? 2 : 1;
            memcpy(id + 516, &n, 4);
        } else if (cmd->cdw10 == 2) {
            if (legacy_list) done.status |= 2u; /* CNS=2 not implemented. */
            else {
                u32 *ids = (u32 *)id;
                if (sparse_namespace && nvme.pci == 0x1000) {
                    if (cmd->nsid < 7) ids[0] = 7;
                    if (cmd->nsid < 0x100000u) ids[cmd->nsid < 7 ? 1 : 0] = 0x100000u;
                } else if (multi_namespace && nvme.pci == 0x1000) {
                    if (cmd->nsid < 1) ids[0] = 1;
                    if (cmd->nsid < 2) ids[cmd->nsid < 1 ? 1 : 0] = 2;
                } else if (!cmd->nsid) ids[0] = 1;
            }
        } else {
            memcpy(id, &disk_sectors, 8);
            memcpy(id + 8, &disk_sectors, 8);
            id[130] = 9;
            if (invalid_namespace && cmd->nsid == 1) done.status |= 2u;
        }
    } else if (qid) {
        u8 opcode = cmd->cdw0 & 255u;
        if (!opcode) ++flushes;
        if (opcode == 1 || opcode == 2) {
            u64 lba = (u64)cmd->cdw10 | (u64)cmd->cdw11 << 32;
            assert(lba < disk_sectors);
            assert(cmd->prp1 == nvme.data_page && !cmd->prp2 && !cmd->cdw12);
            if (fail_io) done.status |= 2u;
            else if (((multi_namespace && cmd->nsid == 1) ||
                      (sparse_namespace && cmd->nsid == 7) || multi_controller) &&
                     nvme.pci == 0x1000) {
                assert(opcode == 2 && lba == 0); /* foreign disk remains read-only */
                memset(phys_ptr((uptr)cmd->prp1), 0, 512);
            }
            else {
                assert(cmd->nsid == (sparse_namespace ? 0x100000u : multi_namespace ? 2u : 1u));
                assert(fseek(disk_file, (long)(lba * 512), SEEK_SET) == 0);
                u8 *buffer = phys_ptr((uptr)cmd->prp1);
                if (opcode == 1) {
                    assert(fwrite(buffer, 1, 512, disk_file) == 512);
                    ++writes;
                } else assert(fread(buffer, 1, 512, disk_file) == 512);
            }
        }
    }
    cq[q->head] = done;
    ++completed;
}


static void reset_admin_model(enum admin_fault fault) {
    for (u32 i = 1; i <= next_page; ++i) assert(!page_live[i]);
    next_page = freed = unsafe_submits = unsafe_dma_reuse = late_consumed = 0;
    multi_namespace = true; multi_controller = sparse_namespace = legacy_list = fail_io = false;
    ready = 0; admin_fault = fault;
    fault_fired = admin_pending = stop_blocked = invalid_namespace = late_posted = false;
}
static void ready_timing_tests(void) {
    /* Controller progress follows elapsed time, independently of how many
     * times a fast CPU can read CSTS. CAP.TO units are 500 ms at 100 Hz. */
    for (u32 enabled = 0; enabled < 2; ++enabled) {
        irq_enabled = enabled; ticks = 10; status_reads = 0;
        ready = 0; ready_target = 1; ready_delay = 25;
        ready_at = ticks; ready_pending = true;
        assert(nvme_wait_ready(true, 1));
        assert(ticks == 35 && irq_enabled == (bool)enabled && !ready_pending);
        assert(status_reads < 2000);
        ready_target = 0; ready_delay = 24;
        ready_at = ticks; ready_pending = true;
        assert(nvme_wait_ready(false, 1));
        assert(ticks == 59 && irq_enabled == (bool)enabled && !ready_pending);
    }
    /* A large advertised budget must not be truncated to eight units. */
    ready = 0; ready_target = 1; ready_delay = 999;
    ready_at = ticks; ready_pending = true;
    assert(nvme_wait_ready(true, 20) && ticks - ready_at == 999);
    /* Zero means a conservative one-unit budget, and a stuck controller
     * consumes that budget without losing the caller's interrupt state. */
    for (u32 enabled = 0; enabled < 2; ++enabled) {
        irq_enabled = enabled; ready = 0; ready_pending = false;
        u64 start = ticks;
        assert(!nvme_wait_ready(true, 0));
        assert(ticks - start == 52 && irq_enabled == (bool)enabled);
    }
    /* An old pending PIC IRQ followed by an imminent fresh PIT edge must
     * not consume any of the advertised real-time readiness interval. */
    const u32 budgets[] = {1, 3, 20};
    for (u32 i = 0; i < sizeof(budgets) / sizeof(*budgets); ++i) {
        elapsed_us = 0; stale_timer_irq = imminent_timer_edge = true;
        u64 start = ticks;
        assert(!nvme_wait_ready(true, budgets[i]));
        assert(ticks - start == budgets[i] * 50u + 2u);
        assert(elapsed_us >= (u64)budgets[i] * 500000u && irq_enabled);
    }
    ticks = ~(u64)0 - 2; ready = 0; ready_target = 1; ready_delay = 4;
    ready_at = ticks; ready_pending = true;
    assert(nvme_wait_ready(true, 1) && ticks == 1 && irq_enabled);
    /* RDY and CFS may both be set. A fatal controller cannot be enabled,
     * while CFS must not prevent recognizing a successful disable. */
    ready_pending = false; ready = 3; u64 start = ticks;
    assert(!nvme_wait_ready(true, 1) && ticks == start && irq_enabled);
    ready = 2;
    assert(nvme_wait_ready(false, 1) && ticks == start && irq_enabled);
    /* Release must retain the same advertised controller budget. A valid
     * disable after 1.25 s cannot be quarantined by a fixed 500 ms wait. */
    ready = 1; controller_ready_delay = 125;
    nvme.regs = dma[0]; nvme.pci = 0x1000; nvme.ready_timeout = 3;
    pci_command[0] = 6; start = ticks;
    assert(nvme_release(true) && ticks - start == 125 && irq_enabled);
    assert(!nvme.regs && !(pci_command[0] & 4u) && !ready_pending);
    controller_ready_delay = 0;
    ready = 0; ticks = 0; irq_enabled = false;
    puts("PASS NVMe readiness: full elapsed CAP.TO budgets, PIT phase/stale IRQ, delayed enable/disable, fatal status, IF and tick wrap");
}
static void admin_ownership_tests(void) {
    const enum admin_fault faults[] = {FATAL_COMPLETION, LIST_TIMEOUT, NAMESPACE_TIMEOUT, BAD_COMPLETION, FATAL_STATUS};
    u64 capacity; u32 pci, nsid;
    for (u32 i = 0; i < sizeof(faults) / sizeof(*faults); ++i) {
        reset_admin_model(faults[i]);
        assert(!nvme_init(&capacity, 0, 1, &pci, &nsid));
        assert(fault_fired && !unsafe_submits && !unsafe_dma_reuse && !late_consumed);
        assert(!admin_pending && !ready && freed == 6 && !(pci_command[0] & 4u));
        assert(nvme_shutdown() && freed == 6); /* no second free */
    }
    /* A valid error CQE releases the old command's pages. An unsupported
     * namespace may be skipped, just as an unsupported CNS=2 uses fallback. */
    reset_admin_model(NO_FAULT); invalid_namespace = true;
    assert(nvme_init(&capacity, 0, 1, &pci, &nsid) && nsid == 2);
    assert(!unsafe_submits && !unsafe_dma_reuse && !late_consumed);
    assert(nvme_shutdown() && freed == 6);
    /* Legacy discovery must also stop after its first timed-out Identify. */
    reset_admin_model(NAMESPACE_TIMEOUT); legacy_list = true;
    assert(!nvme_init(&capacity, 0, 1, &pci, &nsid));
    assert(fault_fired && !unsafe_submits && !unsafe_dma_reuse && !late_consumed && freed == 6);
    /* If RDY never clears, memory remains allocated even after repeated
     * shutdown calls; disabling bus mastering does not authorize reuse. */
    reset_admin_model(NAMESPACE_TIMEOUT); stop_blocked = true;
    assert(!nvme_init(&capacity, 0, 1, &pci, &nsid));
    assert(fault_fired && admin_pending && !unsafe_submits && !unsafe_dma_reuse && !late_consumed);
    assert(!freed && !(pci_command[0] & 4u));
    for (u32 i = 1; i <= next_page; ++i) assert(page_live[i]);
    assert(nvme_shutdown() && nvme_shutdown() && !freed);
    puts("PASS NVMe admin DMA ownership: list/namespace timeouts, late/malformed CQE, fatal status, valid errors and failed-stop quarantine");
}

int main(int argc, char **argv) {
    assert(argc == 3 || argc == 4);
    bool modern = argc == 4;
    multi_namespace = modern && !strcmp(argv[3], "multi");
    multi_controller = modern && !strcmp(argv[3], "controller");
    sparse_namespace = modern && !strcmp(argv[3], "sparse");
    legacy_list = modern && !strcmp(argv[3], "legacy-list");
    disk_file = fopen(argv[1], "r+b");
    assert(disk_file);
    disk_sectors = (u64)strtoul(argv[2], NULL, 10) * 2048;
    u8 id[PAGE] = {0};
    memcpy(id, &disk_sectors, 8);
    memcpy(id + 8, &disk_sectors, 8);
    id[130] = 9;
    u64 blocks = 0;
    assert(nvme_namespace(id, &blocks) && blocks == disk_sectors);
    id[130] = 12; assert(!nvme_namespace(id, &blocks));
    id[130] = 9; id[128] = 8; assert(!nvme_namespace(id, &blocks));
    id[128] = 0; id[29] = 1; assert(!nvme_namespace(id, &blocks));
    ready_timing_tests();
    if (modern && !strcmp(argv[3], "admin-ownership")) {
        admin_ownership_tests(); fclose(disk_file); return 0;
    }
    assert(disk_init() && nvme_disk && (pci_command[nvme.pci == 0x2000] & 6u) == 6u);
    assert(nvme.nsid == (sparse_namespace ? 0x100000u : multi_namespace ? 2u : 1u));
    assert(nvme.pci == (multi_controller ? 0x2000u : 0x1000u));
    assert(disk_volume_count() == 2 && disk_partition_count() == 2);
    assert(completed > 30); /* repeated wraps of both phase and command ID */
    u8 sector[512];
    assert(!disk_volume_read(1, 0, sector) && !memcmp(sector, modern ? "NVSTORE3" : "NVSTORE2", 8));
    assert(disk_write(8, sector) == -NV_EINVAL);
    memset(sector, 0xa7, sizeof(sector));
    assert(!disk_volume_write(0, 10, sector) && writes == 1);
    memset(sector, 0, sizeof(sector));
    assert(!disk_volume_read(0, 10, sector) && sector[0] == 0xa7);
    if (modern) {
        u64 data = volumes[0].geometry.data_first*8;
        memset(sector, 0xb8, sizeof(sector));
        assert(!disk_volume_write(0, data, sector));
        memset(sector, 0, sizeof(sector));
        assert(!disk_volume_read(0, data, sector) && sector[0] == 0xb8);
        assert(disk_volume_write(0, data-1, sector) == -NV_EACCESS);
    }
    assert(!disk_flush() && flushes == 1);
    fail_io = true;
    assert(disk_read(0, sector) == -NV_EIO);
    assert(disk_read(0, sector) == -NV_ENODEV);
    assert(!disk_ready());
    assert(nvme_shutdown());
    assert(!(pci_command[multi_controller] & 4u) && !nvme_ready());
    fclose(disk_file);
    puts("PASS NVMe: PCI, namespace/controller selection, GPT volumes, bounded writes, flush and I/O errors");
}
