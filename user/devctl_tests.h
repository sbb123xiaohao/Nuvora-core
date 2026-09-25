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
    check(devctl(NV_SUB_CPU, 0, NULL) == -NV_ENOSYS &&
              devctl(NV_SUB_NET, NV_NET_INFO, NULL) == -NV_EFAULT &&
              devctl(NV_SUB_NET, 0, NULL) == -NV_EINVAL,
          "CPU reservation and network request validation");
    struct nv_net_info network = {.index = NV_NET_MAX};
    check(devctl(NV_SUB_NET, NV_NET_INFO, &network) == -NV_EINVAL,
          "network enumeration bounds are enforced");
    struct nv_display_info screen_mode = {0};
    int display = nv_display_info(&screen_mode);
    check((display == -NV_ENODEV ||
           (display == 0 && screen_mode.api_version == NV_DISPLAY_API_VERSION &&
            screen_mode.width >= 80 && screen_mode.height >= 25 &&
            screen_mode.max_copy_bytes == NV_DISPLAY_MAX_COPY)) &&
              devctl(NV_SUB_DISPLAY, NV_DISPLAY_INFO, NULL) == -NV_EFAULT &&
              devctl(NV_SUB_DISPLAY, 0, NULL) == -NV_EINVAL,
          "optional framebuffer mode and display request validation");
    struct nv_input_info input = {0};
    struct nv_pointer_event pointer = {0};
    check(nv_input_info(&input) == 0 && input.api_version == NV_INPUT_API_VERSION &&
              input.pointer_devices <= 1 && input.flags == 1 && !input.reserved &&
              devctl(NV_SUB_INPUT, NV_INPUT_INFO, NULL) == -NV_EFAULT &&
              devctl(NV_SUB_INPUT, 0, &input) == -NV_EINVAL,
          "input version, physical pointer enumeration and request validation");
    check(nv_pointer_poll(&pointer) == -NV_EACCESS,
          "pointer reports require exclusive pixel-screen ownership");
    if (display == 0) {
        u32 pixel = nv_display_rgb(screen_mode.format, 0x123456);
        struct nv_display_present rect = {0, 0, 1, 1, 4, (u32)(uptr)&pixel};
        check(nv_display_present(&rect) == -NV_EACCESS &&
                  nv_display_acquire() == 0, "pixel display requires an exclusive lease");
        check(nv_display_present(&rect) == 0, "leased pixel can be presented");
        int polled = nv_pointer_poll(&pointer);
        check((polled == 0 || polled == 1) &&
                  (polled != 1 || (pointer.buttons & ~7u) == 0),
              "pixel-screen owner may poll bounded pointer reports");
        rect.width = screen_mode.width + 1;
        check(nv_display_present(&rect) == -NV_EINVAL &&
                  nv_display_release() == 0 &&
                  nv_display_present(&rect) == -NV_EACCESS,
              "display bounds, ownership and release are enforced");
    } else {
        struct nv_display_present rect = {0};
        check(nv_display_acquire() == -NV_ENODEV, "text-only boot has no pixel lease");
        check(nv_display_present(&rect) == -NV_ENODEV,
              "text-only boot rejects pixel presentation");
        check(nv_display_release() == -NV_EACCESS,
              "text-only boot cannot release another screen");
    }
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
