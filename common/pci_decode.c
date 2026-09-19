#include <nv/pci.h>
#include <nv/string.h>

int pci_decode_display(u32 address, nv_pci_read read, struct nv_gpu_info *out) {
    u32 id = read(address, 0), cls = read(address, 8);
    if ((id & 0xffff) == 0xffff || cls >> 24 != 3)
        return 0;
    memset(out, 0, sizeof(*out));
    out->bus = (address >> 16) & 255;
    out->device = (address >> 11) & 31;
    out->function = (address >> 8) & 7;
    out->vendor = id & 0xffff;
    out->product = id >> 16;
    out->revision = cls & 255;
    out->class_code = cls >> 24;
    out->subclass = (cls >> 16) & 255;
    out->interface = (cls >> 8) & 255;
    u32 status = read(address, 4);
    out->command = status & 0xffff;
    out->status = status >> 16;
    out->state = NV_GPU_DISCOVERED;
    if (out->vendor == 0x10de)
        out->flags |= NV_GPU_NVIDIA;
    if ((read(address, 0x0c) >> 16) & 127) {
        out->flags |= NV_GPU_BAD_HEADER;
        return 1;
    }
    u32 sub = read(address, 0x2c), irq = read(address, 0x3c);
    out->subvendor = sub & 0xffff;
    out->subproduct = sub >> 16;
    out->irq_line = irq & 255;
    out->irq_pin = (irq >> 8) & 255;
    for (u32 i = 0; i < 6; ++i) {
        struct nv_pci_bar *bar = &out->bars[i];
        u32 raw = read(address, 0x10 + 4 * i);
        if (!raw)
            continue;
        if (raw & 1) {
            bar->flags = NV_BAR_IO;
            bar->low = raw & ~3u;
        } else {
            bar->flags = NV_BAR_MEMORY | ((raw & 8) ? NV_BAR_PREFETCH : 0);
            bar->low = raw & ~15u;
            u32 type = (raw >> 1) & 3;
            if (type == 2) {
                bar->flags |= NV_BAR_64;
                if (i == 5) {
                    bar->flags |= NV_BAR_INVALID;
                    continue;
                }
                bar->high = read(address, 0x10 + 4 * ++i);
                out->bars[i].flags = NV_BAR_UPPER;
            } else if (type == 3 || (type == 1 && bar->low >= 0x100000u))
                bar->flags |= NV_BAR_INVALID;
        }
        if (!bar->low && !bar->high)
            bar->flags |= NV_BAR_UNASSIGNED;
    }
    /* Never size BARs by writing all ones: the GPU may be driving the console. */
    if (out->status & 16) {
        u32 next = read(address, 0x34) & 255;
        u64 visited = 0;
        while (next) {
            if (next < 0x40 || next > 0xfc || (next & 3) ||
                (visited & (1ull << (next / 4)))) {
                out->flags |= NV_GPU_BAD_CAPS;
                break;
            }
            visited |= 1ull << (next / 4);
            u32 cap = read(address, next);
            switch (cap & 255) {
            case 5:
                out->capabilities |= NV_PCI_MSI;
                break;
            case 0x11:
                out->capabilities |= NV_PCI_MSIX;
                break;
            case 0x10:
                out->capabilities |= NV_PCI_EXPRESS;
                /* Link registers exist for endpoints and PCIe bridge ports,
                 * but not root-complex integrated endpoints / event collectors. */
                if (((cap >> 20) & 15) != 9 && ((cap >> 20) & 15) != 10) {
                    if (next <= 0xec)
                        out->pcie_link = read(address, next + 0x10) >> 16;
                    else
                        out->flags |= NV_GPU_BAD_CAPS;
                }
                break;
            default:
                break;
            }
            next = (cap >> 8) & 255;
        }
    }
    /* PCIe extended capabilities live in the 4 KiB ECAM function page.
     * A legacy-only reader returns all ones at 0x100, which means unavailable,
     * not malformed. The bounded visited list rejects cycles from bad firmware. */
    u32 next = 0x100;
    u16 visited[64];
    u32 seen = 0;
    while (next) {
        if (next < 0x100 || next > 0xffc || (next & 3)) {
            out->flags |= NV_GPU_BAD_EXT_CAPS;
            break;
        }
        bool duplicate = false;
        for (u32 i = 0; i < seen; ++i)
            if (visited[i] == next)
                duplicate = true;
        if (duplicate || seen == ARRAY_LEN(visited)) {
            out->flags |= NV_GPU_BAD_EXT_CAPS;
            break;
        }
        visited[seen++] = (u16)next;
        u32 cap = read(address, next);
        u32 id = cap & 0xffff;
        if (!cap || cap == 0xffffffffu || id == 0xffff)
            break;
        switch (id) {
        case 0x0001:
            out->ext_capabilities |= NV_PCIE_AER;
            break;
        case 0x000d:
            out->ext_capabilities |= NV_PCIE_ACS;
            break;
        case 0x000f:
            out->ext_capabilities |= NV_PCIE_ATS;
            break;
        case 0x0010:
            out->ext_capabilities |= NV_PCIE_SRIOV;
            break;
        case 0x0015:
            out->ext_capabilities |= NV_PCIE_RESIZABLE_BAR;
            break;
        case 0x001b:
            out->ext_capabilities |= NV_PCIE_PASID;
            break;
        case 0x001d:
            out->ext_capabilities |= NV_PCIE_DPC;
            break;
        default:
            break;
        }
        next = (cap >> 20) & 0xfff;
    }
    return 1;
}
