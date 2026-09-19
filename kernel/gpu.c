#include "kernel.h"
#include <nv/pci.h>

/* Discovery and kernel mapping preparation. No GPU register access, PCI writes,
 * firmware or mode changes. These mappings never become user-accessible. */
static struct nv_gpu_info adapters[NV_GPU_MAX];
static void *bar_mappings[NV_GPU_MAX][6];
static u32 adapter_count, omitted;
static void discover(u32 address, u32 id, u32 cls) {
    (void)id;
    if (cls >> 24 != 3)
        return;
    if (adapter_count == NV_GPU_MAX) {
        ++omitted;
        return;
    }
    pci_decode_display(address, pci_read, &adapters[adapter_count++]);
}
void gpu_init(void) {
    pci_visit(discover);
    kprintf("[ok] PCI display discovery: %u adapter(s), %u omitted\n", adapter_count, omitted);
}
int gpu_get_info(u32 index, struct nv_gpu_info *out) {
    if (index >= NV_GPU_MAX)
        return -NV_EINVAL;
    if (index >= adapter_count)
        return 0;
    *out = adapters[index];
    return 1;
}
static int gpu_map_bar(u32 user_ptr) {
    struct nv_gpu_map_bar_req req;
    struct nv_gpu_map_bar_res res = {0};
    /* Validate the full response before any allocation or copy. Writable x86
     * user mappings are also readable. An 8-byte request alone is too small. */
    if (!user_range(current->pd, user_ptr, sizeof(union nv_gpu_map_bar_io), true))
        return -NV_EFAULT;
    memcpy(&req, (void *)(uptr)user_ptr, sizeof(req));
    int result = -NV_EINVAL;
    if (req.index >= adapter_count || req.bar >= 6)
        goto respond;
    struct nv_pci_bar *bar = &adapters[req.index].bars[req.bar];
    result = -NV_ENODEV;
    if (!(bar->flags & NV_BAR_MEMORY) ||
        (bar->flags & (NV_BAR_UNASSIGNED | NV_BAR_INVALID | NV_BAR_UPPER)))
        goto respond;
    u64 phys = bar->low & ~0xfull;
    if (bar->flags & NV_BAR_64)
        phys |= (u64)bar->high << 32;
    /* Map only a page-aligned first page without touching the device. Its
     * actual valid register range is still unknown until BAR sizing exists.
     * Each boot-snapshot BAR owns at most one permanent kernel window. */
    if (!bar_mappings[req.index][req.bar]) {
        struct nv_cpu_info cpu;
        cpu_get_info(&cpu);
        result = -NV_EIO;
        if (!phys || (phys >> cpu.physical_bits))
            goto respond;
        bar_mappings[req.index][req.bar] = vm_mmio_map(phys, PAGE);
        if (!bar_mappings[req.index][req.bar])
            goto respond;
    }
    res.ok = 1;
    res.length = PAGE;
    result = 1;
respond:
    memcpy((void *)(uptr)user_ptr, &res, sizeof(res));
    return result;
}
int gpu_ioctl(u32 op, u32 user_ptr) {
    switch (op) {
    case NV_GPU_OP_MAP_BAR:
        return gpu_map_bar(user_ptr);
    case NV_GPU_OP_SET_MODE:
    case NV_GPU_OP_PRESENT:
    case NV_GPU_OP_SUBMIT:
        return -NV_ENOSYS; /* scaffolding only; wire up once mode-setting exists */
    default:
        return -NV_EINVAL;
    }
}
