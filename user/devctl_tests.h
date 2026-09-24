/* Guest regression tests for the extensible device-control ABI. */
static bool map_response_empty(const union nv_gpu_map_bar_io *io) {
    return !io->response.ok && !io->response.reserved && !io->response.length;
}
static void devctl_tests(void) {
    union nv_gpu_map_bar_io io, unchanged;
    memset(&io, 0xa5, sizeof(io));
    unchanged = io;
    check(devctl(0, 0, &io) == -NV_EINVAL && devctl(0xffffffffu, 0, &io) == -NV_EINVAL &&
              devctl(NV_SUB_GPU, 0, &io) == -NV_EINVAL &&
              devctl(NV_SUB_GPU, 0xffffffffu, &io) == -NV_EINVAL,
          "DEVCTL rejects unknown subsystems and GPU operations");
    check(devctl(NV_SUB_CPU, 0, NULL) == -NV_ENOSYS && devctl(NV_SUB_NET, 0, NULL) == -NV_EINVAL,
          "reserved CPU and network control return ENOSYS");
    bool unsupported = true;
    for (u32 op = NV_GPU_OP_SET_MODE; op <= NV_GPU_OP_SUBMIT; ++op)
        unsupported &= devctl(NV_SUB_GPU, op, &io) == -NV_ENOSYS;
    check(unsupported && !memcmp(&io, &unchanged, sizeof(io)),
          "unimplemented GPU operations do not modify the request");
    static const union nv_gpu_map_bar_io readonly = {.request = {0, 0}};
    check(devctl(NV_SUB_GPU, NV_GPU_OP_MAP_BAR, NULL) == -NV_EFAULT &&
              devctl(NV_SUB_GPU, NV_GPU_OP_MAP_BAR, (void *)0x100000) == -NV_EFAULT &&
              devctl(NV_SUB_GPU, NV_GPU_OP_MAP_BAR, (void *)0x20000000) == -NV_EFAULT &&
              devctl(NV_SUB_GPU, NV_GPU_OP_MAP_BAR, (void *)&readonly) == -NV_EFAULT &&
              devctl(NV_SUB_GPU, NV_GPU_OP_MAP_BAR, (void *)0x7ffffff8) == -NV_EFAULT &&
              devctl(NV_SUB_GPU, NV_GPU_OP_MAP_BAR, (void *)0xfffffff8) == -NV_EFAULT,
          "BAR output rejects null, kernel, MMIO, read-only and overflowing buffers");
    int wide;
    __asm__ volatile("int $0x81"
                     : "=a"(wide)
                     : "0"(NV_DEVCTL), "b"((u32)NV_SUB_GPU), "c"((u32)NV_GPU_OP_MAP_BAR),
                       "d"(0x100000000ull + (uptr)&io)
                     : "memory", "cc");
    check(wide == -NV_EINVAL, "DEVCTL rejects nonzero high pointer bits");
    io = (union nv_gpu_map_bar_io){.request = {NV_GPU_MAX, 0}};
    bool invalid =
        devctl(NV_SUB_GPU, NV_GPU_OP_MAP_BAR, &io) == -NV_EINVAL && map_response_empty(&io);
    io = (union nv_gpu_map_bar_io){.request = {0, 6}};
    check(invalid && devctl(NV_SUB_GPU, NV_GPU_OP_MAP_BAR, &io) == -NV_EINVAL &&
              map_response_empty(&io),
          "invalid GPU and BAR indices clear the writable response");
    check(gpu_prepare_bar(0, 0, NULL) == -NV_EINVAL, "BAR wrapper requires an output object");

    struct nv_cpu_info cpu;
    cpu_info(&cpu);
    u32 index = NV_GPU_MAX, chosen = 0;
    for (u32 i = 0; i < NV_GPU_MAX && index == NV_GPU_MAX; ++i) {
        struct nv_gpu_info gpu;
        if (gpu_info(i, &gpu) != 1)
            break;
        for (u32 bar = 0; bar < 6; ++bar) {
            const struct nv_pci_bar *b = &gpu.bars[bar];
            u64 physical = ((u64)b->high << 32) | b->low;
            if (!(b->flags & NV_BAR_MEMORY) ||
                (b->flags & (NV_BAR_UNASSIGNED | NV_BAR_INVALID | NV_BAR_UPPER)) || !physical ||
                (physical & (NV_PAGE - 1)) || (physical >> cpu.physical_bits))
                continue;
            index = i;
            chosen = bar;
            break;
        }
    }
    u8 *page = grow(1);
    check((iptr)page > 0, "allocate BAR output-boundary fixture");
    if ((iptr)page < 0)
        return;
    struct nv_gpu_map_bar_req request = {index, chosen};
    memset(page, 0xa5, NV_PAGE);
    memcpy(page + NV_PAGE - sizeof(request), &request, sizeof(request));
    bool rejected = true;
    /* An old implementation leaked a page for each failure here: 600 calls
     * exceeds the complete 512-page MMIO window before the first valid call. */
    for (u32 i = 0; i < 600; ++i)
        rejected &=
            devctl(NV_SUB_GPU, NV_GPU_OP_MAP_BAR, page + NV_PAGE - sizeof(request)) == -NV_EFAULT;
    check(rejected && page[NV_PAGE - sizeof(request) - 1] == 0xa5 &&
              !memcmp(page + NV_PAGE - sizeof(request), &request, sizeof(request)),
          "600 partial-output requests rejected without writes");
    check((iptr)grow(-1) > 0, "BAR boundary fixture released");
    if (index == NV_GPU_MAX) {
        println("SKIP GPU mapping checks: no page-aligned addressable memory BAR");
        return;
    }
    struct {
        u32 before;
        union nv_gpu_map_bar_io io;
        u32 after;
    } guarded = {.before = 0x12345678, .io.request = {index, chosen}, .after = 0x87654321};
    int result = devctl(NV_SUB_GPU, NV_GPU_OP_MAP_BAR, &guarded.io);
    check(result == 1 && guarded.io.response.ok == 1 && !guarded.io.response.reserved &&
              guarded.io.response.length == NV_PAGE && guarded.before == 0x12345678 &&
              guarded.after == 0x87654321,
          "first valid BAR mapping survives 600 failed requests");
    bool reused = true;
    struct nv_gpu_map_bar_res response;
    for (u32 i = 0; i < 600; ++i)
        reused &= gpu_prepare_bar(index, chosen, &response) == 1 && response.ok == 1 &&
                  response.length == NV_PAGE && !response.reserved;
    check(reused, "600 repeated BAR requests reuse the same kernel window");
    bool classified = true;
    struct nv_gpu_info gpu;
    gpu_info(index, &gpu);
    for (u32 bar = 0; bar < 6; ++bar) {
        u32 flags = gpu.bars[bar].flags;
        if ((flags & NV_BAR_MEMORY) &&
            !(flags & (NV_BAR_UNASSIGNED | NV_BAR_INVALID | NV_BAR_UPPER)))
            continue;
        io = (union nv_gpu_map_bar_io){.request = {index, bar}};
        classified &=
            devctl(NV_SUB_GPU, NV_GPU_OP_MAP_BAR, &io) == -NV_ENODEV && map_response_empty(&io);
    }
    check(classified, "absent, I/O and non-resource BARs return a zero error response");
    int pid = spawn("/apps/fault", "mmio");
    check(pid > 0 && wait_task(pid) == 142, "prepared MMIO remains supervisor-only");
}
