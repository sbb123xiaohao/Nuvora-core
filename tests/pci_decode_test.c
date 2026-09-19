#include <nv/pci.h>
#include <nv/string.h>
extern int printf(const char *, ...);
static u32 config[1024], reads, failures, checks;
static bool extended;
static u32 read_config(u32 address, u32 off) {
    (void)address;
    ++reads;
    if (off > 4092 || (off & 3) || reads > 200) {
        ++failures;
        return 0;
    }
    if (off > 252 && !extended)
        return 0xffffffffu;
    return config[off / 4];
}
static void expect(bool value, const char *name) {
    ++checks;
    if (!value) {
        ++failures;
        printf("FAIL %s\n", name);
    }
}
static void base(void) {
    memset(config, 0, sizeof(config));
    config[0] = 0x268410de;
    config[2] = 0x030200a1;
    config[0x2c / 4] = 0x456710de;
    reads = 0;
    extended = false;
}
int main(void) {
    struct nv_gpu_info g;
    base();
    config[0] = 0xffffffff;
    expect(!pci_decode_display(0, read_config, &g), "absent device");
    base();
    config[2] = 0x040300a1;
    expect(!pci_decode_display(0, read_config, &g), "NVIDIA audio function is not a GPU");
    base();
    expect(pci_decode_display(0x021900, read_config, &g) == 1 && (g.flags & NV_GPU_NVIDIA) &&
               g.vendor == 0x10de && g.product == 0x2684 && g.bus == 2 && g.device == 3 &&
               g.function == 1 && g.subclass == 2 && g.subvendor == 0x10de &&
               g.subproduct == 0x4567 && g.revision == 0xa1,
           "synthetic NVIDIA identity and multifunction BDF");
    expect(g.state == NV_GPU_DISCOVERED, "discovery does not claim a working driver");
    base();
    config[0] = 0x11111234;
    config[2] = 0x03000002;
    pci_decode_display(0, read_config, &g);
    expect(!(g.flags & NV_GPU_NVIDIA) && g.subclass == 0, "generic VGA classification");
    base();
    config[4] = 0xf0000000;
    config[5] = 0x4560000c;
    config[6] = 0x123;
    config[7] = 0x0000c001;
    pci_decode_display(0, read_config, &g);
    expect(g.bars[0].low == 0xf0000000 && g.bars[0].flags == NV_BAR_MEMORY, "32-bit MMIO BAR");
    expect(g.bars[1].low == 0x45600000 && g.bars[1].high == 0x123 &&
               g.bars[1].flags == (NV_BAR_MEMORY | NV_BAR_64 | NV_BAR_PREFETCH),
           "64-bit BAR above 4 GiB");
    expect(g.bars[2].flags == NV_BAR_UPPER && !g.bars[2].low, "64-bit BAR consumes its high half");
    expect(g.bars[3].flags == NV_BAR_IO && g.bars[3].low == 0xc000, "I/O BAR");
    expect(!g.bars[4].flags && !g.bars[5].flags, "zero BARs do not imply VRAM");
    base();
    config[4] = 4;
    config[9] = 4;
    pci_decode_display(0, read_config, &g);
    expect(g.bars[0].flags & NV_BAR_UNASSIGNED, "unassigned 64-bit BAR");
    expect(g.bars[5].flags & NV_BAR_INVALID, "BAR5 cannot start a 64-bit pair");
    base();
    config[4] = 6;
    pci_decode_display(0, read_config, &g);
    expect(g.bars[0].flags & NV_BAR_INVALID, "reserved BAR encoding");
    base();
    config[1] = 0x00100003;
    config[0x34 / 4] = 0x40;
    config[0x40 / 4] = 0x7010;
    config[0x50 / 4] = 0x00830000;
    config[0x70 / 4] = 0x8005;
    config[0x80 / 4] = 0x11;
    pci_decode_display(0, read_config, &g);
    expect(g.capabilities == (NV_PCI_MSI | NV_PCI_MSIX | NV_PCI_EXPRESS),
           "bounded capabilities traversal");
    expect(g.pcie_link == 0x83, "PCIe link status");
    config[0x80 / 4] = 0x4011;
    reads = 0;
    pci_decode_display(0, read_config, &g);
    expect((g.flags & NV_GPU_BAD_CAPS) && reads < 40, "capability cycle terminates");
    for (u32 i = 0; i < 3; ++i) {
        base();
        config[1] = 0x100000;
        config[0x34 / 4] = (u32[]){0x3c, 0x41, 0xff}[i];
        pci_decode_display(0, read_config, &g);
        expect(g.flags & NV_GPU_BAD_CAPS, "out-of-range or unaligned capability");
    }
    base();
    config[1] = 0x100000;
    config[0x34 / 4] = 0xf8;
    config[0xf8 / 4] = 0x10;
    pci_decode_display(0, read_config, &g);
    expect(g.flags & NV_GPU_BAD_CAPS, "truncated PCIe capability");
    base();
    config[3] = 0x00010000;
    pci_decode_display(0, read_config, &g);
    expect((g.flags & NV_GPU_BAD_HEADER) && !g.bars[0].flags,
           "unexpected header leaves resources untouched");
    base();
    pci_decode_display(0, read_config, &g);
    expect(!g.ext_capabilities && !(g.flags & NV_GPU_BAD_EXT_CAPS),
           "legacy config access reports extended capabilities unavailable");
    base();
    extended = true;
    config[0x100 / 4] = (0x140u << 20) | (1u << 16) | 0x0001;
    config[0x140 / 4] = (0x180u << 20) | (1u << 16) | 0x0015;
    config[0x180 / 4] = (0x1c0u << 20) | (1u << 16) | 0x000d;
    config[0x1c0 / 4] = (0x200u << 20) | (1u << 16) | 0x000f;
    config[0x200 / 4] = (0x240u << 20) | (1u << 16) | 0x0010;
    config[0x240 / 4] = (0x280u << 20) | (1u << 16) | 0x001b;
    config[0x280 / 4] = (1u << 16) | 0x001d;
    pci_decode_display(0, read_config, &g);
    expect(g.ext_capabilities == (NV_PCIE_AER | NV_PCIE_RESIZABLE_BAR | NV_PCIE_ACS |
                                  NV_PCIE_ATS | NV_PCIE_SRIOV | NV_PCIE_PASID | NV_PCIE_DPC),
           "PCIe 4 KiB extended capability traversal");
    base();
    extended = true;
    config[0x100 / 4] = (0x140u << 20) | (1u << 16) | 1;
    config[0x140 / 4] = (0x100u << 20) | (1u << 16) | 0x15;
    pci_decode_display(0, read_config, &g);
    expect((g.flags & NV_GPU_BAD_EXT_CAPS) && reads < 30,
           "extended capability cycle terminates");
    base();
    extended = true;
    config[0x100 / 4] = (0x102u << 20) | (1u << 16) | 1;
    pci_decode_display(0, read_config, &g);
    expect(g.flags & NV_GPU_BAD_EXT_CAPS, "unaligned extended capability rejected");
    printf("PCI DECODER: %u checks, %u failures (synthetic data; no physical NVIDIA GPU)\n", checks,
           failures);
    return failures ? 1 : 0;
}
