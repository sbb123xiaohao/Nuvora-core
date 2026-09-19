static int show_cpu(void) {
    struct nv_cpu_info c;
    int r = cpu_info(&c);
    if (r < 0)
        return r;
    print("CPU: ");
    print(c.vendor);
    print(" / ");
    println(c.brand);
    print("Family ");
    print_u32(c.family);
    print(", model ");
    print_u32(c.model);
    print(", stepping ");
    print_u32(c.stepping);
    print("; kernel ");
    print_u32(c.bits);
    println(" bit");
    print("Online CPUs: ");
    print_u32(c.online_cpus);
    println(" (single-CPU scheduler)");
    print("Physical address bits: ");
    print_u32(c.physical_bits);
#ifdef __x86_64__
    println("; managed RAM limit: 64 GiB");
#else
    println("; managed RAM limit: 192 MiB");
#endif
    print("State format: ");
    println(c.fp_mode == NV_FP_FXSAVE ? "FXSAVE (512 bytes)" : "x87 FNSAVE (108 bytes)");
    print("Enabled context: x87");
    if (c.usable & NV_CPU_MMX)
        print(" MMX");
    if (c.usable & NV_CPU_SSE)
        print(" SSE");
    if (c.usable & NV_CPU_SSE2)
        print(" SSE2");
    println("; eager save/restore on every task switch");
    print("Hardware AVX: ");
    print(c.features_ecx & (1u << 28) ? "yes" : "no");
    println("; OS AVX/XSAVE: disabled");
    print("Hardware NX: ");
    print(c.extended_edx & (1u << 20) ? "yes" : "no");
#ifdef __x86_64__
    println("; page NX: enabled");
#else
    println("; page NX: unavailable with i686 non-PAE paging");
#endif
    return 0;
}
static void print_address(u32 high, u32 low) {
    static const char hex[] = "0123456789abcdef";
    char s[19] = "0x";
    for (u32 i = 0; i < 8; ++i) {
        s[2 + i] = hex[(high >> (28 - 4 * i)) & 15];
        s[10 + i] = hex[(low >> (28 - 4 * i)) & 15];
    }
    s[18] = 0;
    print(s);
}
static int show_platform(void) {
    struct nv_platform_info p;
    int r = platform_info(&p);
    if (r < 0)
        return r;
    if (p.flags & NV_PLATFORM_ACPI) {
        print("Firmware: ACPI ");
        if (!p.acpi_revision)
            print("1.0 (RSDP revision 0)");
        else {
            print("RSDP revision ");
            print_u32(p.acpi_revision);
        }
        print(p.flags & NV_PLATFORM_XSDT ? " via XSDT, OEM=" : " via RSDT, OEM=");
        println(*p.oem_id ? p.oem_id : "unknown");
        print("ACPI table OEM: ");
        println(*p.oem_table_id ? p.oem_table_id : "unknown");
        print("MCFG: ");
        print_u32(p.mcfg_entries);
        print(" firmware entry(s), ");
        print_u32(p.ecam_regions);
        print(" active, ");
        print_u32(p.rejected_entries);
        println(" rejected.");
    } else {
        println("Firmware: no valid ACPI root discovered.");
    }
    if (p.flags & NV_PLATFORM_ECAM) {
        print("PCI config: ECAM, 4096 bytes per function; segment ");
        print_u32(p.segment);
        print(" buses ");
        print_u32(p.start_bus);
        print("-");
        print_u32(p.end_bus);
        print(" at ");
        print_address(p.base_high, p.base_low);
        println(".");
        println("Fallback: CF8/CFC remains active for uncovered segment-0 buses.");
    } else {
        println("PCI config: CF8/CFC legacy access, 256 bytes per function.");
        if (p.flags & NV_PLATFORM_ECAM_DISABLED)
            println("ECAM was disabled by the nv.no-ecam=1 boot option.");
    }
    println("Tables are checksum/length validated; ACPI AML and power management are not enabled.");
    return 0;
}
static int show_gpu(void) {
    u32 count = 0;
    for (u32 i = 0; i < NV_GPU_MAX; ++i) {
        struct nv_gpu_info g;
        int r = gpu_info(i, &g);
        if (r < 0)
            return r;
        if (!r)
            break;
        ++count;
        print("GPU ");
        print_u32(i);
        print(": ");
        print(g.flags & NV_GPU_NVIDIA ? "NVIDIA" : "PCI display");
        print(" vendor=");
        print_hex(g.vendor);
        print(" device=");
        print_hex(g.product);
        print(" at ");
        print_u32(g.bus);
        print(":");
        print_u32(g.device);
        print(".");
        print_u32(g.function);
        print(" class=3/");
        print_u32(g.subclass);
        print(" revision=");
        print_hex(g.revision);
        print("\n");
        print("  Subsystem ");
        print_hex(g.subvendor);
        print(":");
        print_hex(g.subproduct);
        print(" command=");
        print_hex(g.command);
        print(" IRQ=");
        print_u32(g.irq_line);
        print("\n");
        print("  Capabilities:");
        if (g.capabilities & NV_PCI_MSI)
            print(" MSI");
        if (g.capabilities & NV_PCI_MSIX)
            print(" MSI-X");
        if (g.capabilities & NV_PCI_EXPRESS) {
            print(" PCIe link-code=");
            print_u32(g.pcie_link & 15);
            print(" width=x");
            print_u32((g.pcie_link >> 4) & 63);
        }
        if (!g.capabilities)
            print(" none reported");
        print("\n");
        print("  PCIe extended:");
        if (g.ext_capabilities & NV_PCIE_AER)
            print(" AER");
        if (g.ext_capabilities & NV_PCIE_ACS)
            print(" ACS");
        if (g.ext_capabilities & NV_PCIE_ATS)
            print(" ATS");
        if (g.ext_capabilities & NV_PCIE_SRIOV)
            print(" SR-IOV");
        if (g.ext_capabilities & NV_PCIE_RESIZABLE_BAR)
            print(" Resizable-BAR");
        if (g.ext_capabilities & NV_PCIE_PASID)
            print(" PASID");
        if (g.ext_capabilities & NV_PCIE_DPC)
            print(" DPC");
        if (!g.ext_capabilities)
            print(" none reported or legacy access only");
        print("\n");
        for (u32 j = 0; j < 6; ++j) {
            struct nv_pci_bar *b = &g.bars[j];
            if (!b->flags || (b->flags & NV_BAR_UPPER))
                continue;
            print("  BAR");
            print_u32(j);
            print(": ");
            print_address(b->high, b->low);
            print(b->flags & NV_BAR_IO ? " I/O" : (b->flags & NV_BAR_64 ? " MEM64" : " MEM32"));
            if (b->flags & NV_BAR_PREFETCH)
                print(" prefetchable");
            if (b->flags & NV_BAR_UNASSIGNED)
                print(" unassigned");
            if (b->flags & NV_BAR_INVALID)
                print(" invalid encoding");
            print("\n");
        }
        if (g.flags & NV_GPU_BAD_CAPS)
            println("  Capability list malformed; decoding stopped.");
        if (g.flags & NV_GPU_BAD_EXT_CAPS)
            println("  PCIe extended capability list malformed; decoding stopped.");
        if (g.flags & NV_GPU_BAD_HEADER)
            println("  Unsupported PCI header; resource decoding skipped.");
        println("  Driver: discovery only; BAR sizes and VRAM capacity unknown.");
    }
    print_u32(count);
    println(" display adapter(s). Boot snapshot, up to 16 adapters.");
    println("Console: firmware framebuffer or VGA text, plus serial. No GPU modesetting or acceleration.");
    return 0;
}
