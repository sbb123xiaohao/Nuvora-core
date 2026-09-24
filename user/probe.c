#include "runtime.h"
static u32 passed, failed;
static void check(bool ok, const char *what) {
    print(ok ? "PASS " : "FAIL ");
    println(what);
    if (ok)
        ++passed;
    else
        ++failed;
}
static void settle(void) {
    yield();
    nap(20);
    yield();
}
#include "devctl_tests.h"
static void abi_tests(void) {
    struct nv_info s;
    check(info(&s) == 0 && s.abi == NV_ABI_VERSION, "private syscall ABI");
    check(call(0xffff, 0, 0, 0) == -NV_ENOSYS, "unknown syscall rejected");
    check(call(NV_EMIT, 1, 0x100000, 16) == -NV_EFAULT, "supervisor pointer rejected");
    check(call(NV_EMIT, 1, 0x7ffffff0, 32) == -NV_EFAULT, "user boundary overflow rejected");
    check(call(NV_EMIT, 1, 0xfffffff0, 32) == -NV_EFAULT, "wrapped pointer rejected");
    check(call(NV_EMIT, 1, 0, 0) == 0, "zero-length I/O has no pointer access");
    check(call(NV_EMIT, 1, 0, 16385) == -NV_E2BIG, "I/O work bounded");
    check(call(NV_OPEN, 0x100000, NV_READ, 0) == -NV_EFAULT, "kernel path pointer rejected");
    check(call(NV_INFO, (uptr)abi_tests, 0, 0) == -NV_EFAULT, "read-only output buffer rejected");
#ifdef __x86_64__
    int wide_result;
    __asm__ volatile("int $0x81"
                     : "=a"(wide_result)
                     : "0"(NV_INFO), "b"(0x100000000ull + (uptr)&s), "c"(0u), "d"(0u)
                     : "memory", "cc");
    check(sizeof(void *) == 8 && wide_result == -NV_EINVAL,
          "64-bit code and high ABI bits checked");
#endif
    check(open_file("/apps/pulse", NV_WRITE) == -NV_EACCESS, "embedded programs read-only");
    check(open_file("/apps/pulse", NV_READ | NV_TRUNC) == -NV_EINVAL,
          "truncation requires write access");
    check(nap(0xffffffff) == -NV_EINVAL, "sleep overflow rejected");
    struct nv_usb_controller uc;
    struct nv_usb_device ud;
    check(call(NV_USB, 0, 0, 0) == -NV_EINVAL && call(NV_USB, NV_USB_RESCAN, 1, 0) == -NV_EINVAL &&
              call(NV_USB, NV_USB_RESCAN, 0, 1) == -NV_EINVAL,
          "USB operation and reserved arguments checked");
    check(usb_controller(NV_USB_CONTROLLER_MAX, &uc) == -NV_EINVAL &&
              usb_device(NV_USB_DEVICE_MAX, &ud) == -NV_EINVAL,
          "USB enumeration indices bounded");
    check(call(NV_USB, NV_USB_CONTROLLERS, 0, 0x100000) == -NV_EFAULT &&
              call(NV_USB, NV_USB_DEVICES, 0, 0x20000000) == -NV_EFAULT,
          "USB output cannot target kernel or MMIO memory");
    check(call(NV_USB, NV_USB_DEVICES, 0, (uptr)abi_tests) == -NV_EFAULT,
          "USB output requires writable user pages");
    check(call(NV_USB, NV_USB_DEVICES, 0, 0x7ffffff0) == -NV_EFAULT &&
              call(NV_USB, NV_USB_CONTROLLERS, 0, 0xfffffff0) == -NV_EFAULT,
          "USB output boundary and arithmetic overflow checked");
    u8 *usb_page = grow(1);
    bool usb_cross = (iptr)usb_page >= 0;
    if (usb_cross) {
        memset(usb_page, 0xa5, NV_PAGE);
        usb_cross = call(NV_USB, NV_USB_DEVICES, 0, (uptr)usb_page + NV_PAGE - sizeof(ud) + 1) ==
                    -NV_EFAULT;
        for (u32 i = 0; i < NV_PAGE; ++i)
            usb_cross = usb_cross && usb_page[i] == 0xa5;
        usb_cross = (iptr)grow(-1) >= 0 && usb_cross;
    }
    check(usb_cross, "USB cross-page output rejected before any write");
    char longpath[NV_PATH_MAX + 1];
    memset(longpath, 'x', sizeof(longpath));
    longpath[NV_PATH_MAX] = 0;
    check(open_file(longpath, NV_READ) == -NV_E2BIG, "unterminated path bounded");
}
static void filesystem_tests(void) {
    char cwd[NV_PATH_MAX], buf[128] = {0};
    check(mkdir_path("/tmp/probe") == 0, "create directory");
    check(mkdir_path("/tmp/probe") == -NV_EEXIST, "duplicate directory rejected");
    check(chdir_path("/tmp/probe") == 0 && getcwd_path(cwd, sizeof(cwd)) == 0 &&
              !strcmp(cwd, "/tmp/probe"),
          "per-process current directory");
    check(mkdir_path("sub") == 0 && chdir_path("sub/.././") == 0, "relative dot path resolution");
    int fd = open_file("note", NV_READ | NV_WRITE | NV_CREATE | NV_EXCL);
    check(fd >= 3, "exclusive file creation");
    check(open_file("note", NV_WRITE | NV_CREATE | NV_EXCL) == -NV_EEXIST,
          "exclusive create is atomic");
    check(emit(fd, "alpha", 5) == 5 && seek_file(fd, 0, 0) == 0 && take(fd, buf, 5) == 5 &&
              !memcmp(buf, "alpha", 5),
          "file write seek read");
    check(seek_file(fd, 100, 0) == 100 && emit(fd, "z", 1) == 1 && seek_file(fd, 5, 0) == 5 &&
              take(fd, buf, 96) == 96 && buf[95] == 'z',
          "sparse file length");
    bool zeros = true;
    for (u32 i = 0; i < 95; ++i)
        if (buf[i])
            zeros = false;
    check(zeros, "sparse holes zero-filled");
    check(seek_file(fd, (i32)0x80000000u, 0) == -NV_EINVAL, "negative seek overflow rejected");
    check(seek_file(fd, (i32)NV_FILE_MAX, 0) == (int)NV_FILE_MAX && emit(fd, "x", 1) == -NV_ENOSPC,
          "file size limit enforced");
    check(remove_path("note") == -NV_EBUSY, "open file cannot be removed");
    check(close_file(fd) == 0 && close_file(fd) == -NV_EBADF, "close and double-close handling");
    fd = open_file("note", NV_WRITE | NV_TRUNC);
    check(fd >= 3 && close_file(fd) == 0, "truncate file");
    fd = open_file("note", NV_WRITE | NV_APPEND);
    emit(fd, "one", 3);
    close_file(fd);
    fd = open_file("note", NV_WRITE | NV_APPEND);
    seek_file(fd, 0, 0);
    emit(fd, "two", 3);
    close_file(fd);
    fd = open_file("note", NV_READ);
    memset(buf, 0, sizeof(buf));
    int nr = take(fd, buf, sizeof(buf));
    close_file(fd);
    check(nr == 6 && !strcmp(buf, "onetwo"), "append ignores a stale seek offset");
    check(copy_file("note", "copy") == 0 && copy_file("note", "note") == -NV_EEXIST,
          "copy refuses destination overwrite");
    check(move_path("copy", "sub/moved") == 0 && open_file("copy", NV_READ) == -NV_ENOENT,
          "move file across directories");
    check(move_path("sub", "sub/loop") == -NV_EINVAL, "directory cycle rejected");
    check(remove_path("sub") == -NV_ENOTEMPTY, "nonempty directory removal rejected");
    check(remove_path(".") == -NV_EBUSY, "active working directory protected");
    check(open_file("note/", NV_READ) == -NV_ENOTDIR, "trailing slash requires directory");
    struct nv_dirent ent;
    check(list_dir("sub", 0, &ent) == 1 && !strcmp(ent.name, "moved") && ent.kind == NV_FILE,
          "directory enumeration");
    check(list_dir("sub", 1, &ent) == 0, "directory enumeration end");
    check(remove_path("sub/moved") == 0 && remove_path("sub") == 0 && remove_path("note") == 0,
          "remove files and empty directory");
    check(chdir_path("../../../../") == 0 && getcwd_path(cwd, sizeof(cwd)) == 0 &&
              !strcmp(cwd, "/"),
          "parent traversal stays at root");
    int zero = open_file("/dev/zero", NV_READ);
    memset(buf, 0xa5, sizeof(buf));
    nr = take(zero, buf, sizeof(buf));
    close_file(zero);
    zeros = true;
    for (u32 i = 0; i < sizeof(buf); ++i)
        if (buf[i])
            zeros = false;
    check(nr == sizeof(buf) && zeros, "zero device");
    int null = open_file("/dev/null", NV_READ | NV_WRITE);
    check(emit(null, "discard", 7) == 7 && take(null, buf, 1) == 0, "null device");
    close_file(null);
    fd = open_file("/sys/memory", NV_READ);
    nr = take(fd, buf, sizeof(buf));
    close_file(fd);
    check(nr > 10 && !memcmp(buf, "ram_pages", 9), "live memory proc view");
    int handles[NV_OPEN_MAX], count = 0;
    for (int i = 0; i < NV_OPEN_MAX; ++i) {
        int h = open_file("/dev/null", NV_READ);
        if (h < 0)
            break;
        handles[count++] = h;
    }
    check(count == NV_OPEN_MAX - 3 && open_file("/dev/null", NV_READ) == -NV_ENOSPC,
          "per-process descriptor bound");
    for (int i = 0; i < count; ++i)
        close_file(handles[i]);
    check(remove_path("/tmp/probe") == 0 && chdir_path("/home") == 0, "filesystem test cleanup");
}
static void memory_tests(void) {
    struct nv_info before, after;
    info(&before);
    u8 *p = grow(2);
    check((i32)(uptr)p > 0, "allocate user heap pages");
    if ((i32)(uptr)p < 0)
        return;
    bool zero = true;
    for (u32 i = 0; i < 2 * NV_PAGE; ++i)
        if (p[i])
            zero = false;
    check(zero, "new user pages are zeroed");
    p[0] = 0x5a;
    p[2 * NV_PAGE - 1] = 0xa5;
    check(p[0] == 0x5a && p[2 * NV_PAGE - 1] == 0xa5, "user heap read/write");
    check((i32)(uptr)grow(-1) > 0, "shrink user heap");
    int fd = open_file("/dev/zero", NV_READ);
    p[NV_PAGE - 1] = 0x5a;
    check(take(fd, p + NV_PAGE - 1, 2) == -NV_EFAULT && p[NV_PAGE - 1] == 0x5a,
          "cross-page output validation is complete");
    close_file(fd);
    check(call(NV_GROW, 0x80000000u, 0, 0) == -NV_EINVAL, "heap shrink overflow rejected");
    check(call(NV_GROW, 1025, 0, 0) == -NV_ENOMEM, "per-process heap bound");
    check((iptr)grow(1025) == -NV_ENOMEM, "native pointer wrapper preserves allocation errors");
    int pid = spawn("/apps/fault", "peer");
    check(pid > 0 && wait_task(pid) == 142, "separate process address spaces");
    settle();
    grow(-1);
    info(&after);
    check(after.free_pages == before.free_pages, "user pages and page tables reclaimed");
    static const u32 lengths[] = {1, 511, 512, 513, 1023, 1024};
    bool contents = true, reclaimed = true;
    for (u32 round = 0; round < 2 * ARRAY_LEN(lengths); ++round) {
        u32 count = lengths[round % ARRAY_LEN(lengths)];
        u8 *span = grow((i32)count);
        if ((iptr)span < 0) {
            contents = false;
            break;
        }
        for (u32 page = 0; page < count; ++page) {
            u8 *item = span + page * NV_PAGE;
            contents = contents && item[0] == 0 && item[NV_PAGE - 1] == 0;
            item[0] = 0xa5; item[NV_PAGE - 1] = 0x5a;
        }
        reclaimed = (iptr)grow(-(i32)count) > 0 && reclaimed;
        reclaimed = call(NV_EMIT, 1, (uptr)span, 1) == -NV_EFAULT && reclaimed;
        info(&after);
        reclaimed = after.free_pages == before.free_pages && reclaimed;
    }
    check(contents, "repeated heap growth across page-table boundaries starts zeroed");
    check(reclaimed, "full heap shrink removes mappings and recovers every page table");
}
static void process_tests(void) {
    int pid = spawn("/apps/pulse", "quiet");
    check(pid > 0 && wait_task(pid) == 7, "ELF launch and child exit status");
    check(wait_task(pid) == -NV_ECHILD, "wait status consumed once");
    check(wait_task(1) == -NV_ECHILD, "cannot wait for unrelated process");
    static const char *const faults[] = {"null",   "kernel", "text",  "io",
                                         "divide", "opcode", "guard", "fpu"};
    static const u32 codes[] = {142, 142, 142, 141, 128, 134, 142, 144};
    for (u32 i = 0; i < ARRAY_LEN(faults); ++i) {
        pid = spawn("/apps/fault", faults[i]);
        int r = pid > 0 ? wait_task(pid) : pid;
        print("Fault case: ");
        println(faults[i]);
        check(r == (int)codes[i], "fault contained to child");
    }
#ifdef __x86_64__
    pid = spawn("/apps/fault", "heap-exec");
    check(pid > 0 && wait_task(pid) == 142, "NX prevents execution from user heap");
    pid = spawn("/apps/fault", "stack-exec");
    check(pid > 0 && wait_task(pid) == 142, "NX prevents execution from user stack");
    pid = spawn("/apps/fault", "physical-alias");
    check(pid > 0 && wait_task(pid) == 142, "physical RAM alias is supervisor-only");
    pid = spawn("/apps/fault", "kernel-heap");
    check(pid > 0 && wait_task(pid) == 142, "kernel heap mapping is supervisor-only");
#endif
    int a = spawn("/apps/spin", ""), b = spawn("/apps/spin", "");
    u32 start = clock_ticks();
    nap(350);
    u32 at = 0, bt = 0;
    for (u32 i = 0; i < NV_TASK_MAX; ++i) {
        struct nv_taskinfo t;
        if (task_at(i, &t) > 0) {
            if (t.pid == (u32)a)
                at = t.cpu_ticks;
            if (t.pid == (u32)b)
                bt = t.cpu_ticks;
        }
    }
    check(a > 0 && b > 0 && at > 0 && bt > 0 && clock_ticks() - start >= 35,
          "timer preempts two syscall-free busy loops");
#ifdef __x86_64__
    extern int wide_register_test(void);
    check(wide_register_test() == 1, "full 64-bit registers survive scheduling");
#endif
    check(stop_task(a) == 0 && stop_task(b) == 0 && wait_task(a) == 143 && wait_task(b) == 143,
          "terminate and reap busy processes");
    check(stop_task(1) == -NV_EACCESS, "initial process protected");
    settle();
    struct nv_info before, after;
    info(&before);
    bool ok = true;
    for (u32 i = 0; i < 40; ++i) {
        pid = spawn("/apps/pulse", "quiet");
        if (pid < 0 || wait_task(pid) != 7) {
            ok = false;
            break;
        }
        settle();
    }
    info(&after);
    check(ok && before.free_pages == after.free_pages && before.heap_used == after.heap_used,
          "40 spawn/wait cycles release all memory");
    int children[NV_TASK_MAX], count = 0, last = 0;
    for (u32 i = 0; i < NV_TASK_MAX; ++i) {
        pid = spawn("/apps/spin", "");
        if (pid < 0) {
            last = pid;
            break;
        }
        children[count++] = pid;
    }
    check(count >= 24 && last == -NV_ENOSPC, "process table exhaustion returns an error");
    for (int i = 0; i < count; ++i) {
        stop_task(children[i]);
        wait_task(children[i]);
    }
    settle();
    info(&after);
    check(before.free_pages == after.free_pages && before.heap_used == after.heap_used,
          "process exhaustion recovery has no leak");
}
static void executable_tests(void) {
    int fd = open_file("/tmp/bad-elf", NV_WRITE | NV_CREATE | NV_EXCL);
    emit(fd, "not-an-elf", 10);
    close_file(fd);
    check(spawn("/tmp/bad-elf", "") == -NV_ENOEXEC, "truncated executable rejected");
    remove_path("/tmp/bad-elf");
    check(copy_file("/apps/pulse", "/tmp/bad-elf") == 0, "prepare malformed ELF fixture");
    fd = open_file("/tmp/bad-elf", NV_WRITE);
    u32 off = 0xfffffff0u;
#ifdef __x86_64__
    seek_file(fd, 32, 0);
#else
    seek_file(fd, 28, 0);
#endif
    emit(fd, &off, 4);
    close_file(fd);
    struct nv_info a, b;
    info(&a);
    bool ok = true;
    for (u32 i = 0; i < 20; ++i)
        if (spawn("/tmp/bad-elf", "") != -NV_ENOEXEC)
            ok = false;
    info(&b);
    check(ok && a.free_pages == b.free_pages && a.heap_used == b.heap_used,
          "malformed ELF offsets rejected without leaks");
    remove_path("/tmp/bad-elf");
    copy_file("/apps/pulse", "/tmp/bad-elf");
    fd = open_file("/tmp/bad-elf", NV_WRITE);
    off = 0x100000;
    seek_file(fd, 24, 0);
    emit(fd, &off, 4);
    close_file(fd);
    info(&a);
    check(spawn("/tmp/bad-elf", "") == -NV_ENOEXEC, "kernel-address ELF entry rejected");
    info(&b);
    check(a.free_pages == b.free_pages && a.heap_used == b.heap_used,
          "failed ELF load rolls back mapped pages");
    remove_path("/tmp/bad-elf");
}
static void tokenizer_tests(void) {
    char line[96] = "weave 'two words' \"hello world\"";
    char *v[8];
    int n = tokenize(line, v, 8);
    check(n == 3 && !strcmp(v[1], "two words") && !strcmp(v[2], "hello world"),
          "shell quoted arguments");
    strlcpy(line, "weave a hello\\ world", sizeof(line));
    n = tokenize(line, v, 8);
    check(n == 3 && !strcmp(v[2], "hello world"), "shell escaped spaces");
    strlcpy(line, "weave 'unterminated", sizeof(line));
    check(tokenize(line, v, 8) == -NV_EINVAL, "shell unmatched quote rejected");
    strlcpy(line, "a b c", sizeof(line));
    check(tokenize(line, v, 2) == -NV_E2BIG, "shell argument count bounded");
    check(crc32("123456789", 9) == 0xcbf43926u, "CRC32 standard check vector");
}
static void core_regressions(void) {
    struct nv_info before, after;
    settle();
    info(&before);
    int pid = spawn("/apps/pulse", "quiet");
    nap(100);
    bool zombie = false;
    u32 pages = 1;
    for (u32 i = 0; i < NV_TASK_MAX; ++i) {
        struct nv_taskinfo t;
        if (task_at(i, &t) > 0 && t.pid == (u32)pid) {
            zombie = t.state == NV_ZOMBIE;
            pages = t.pages;
        }
    }
    info(&after);
    check(zombie && !pages && before.free_pages == after.free_pages &&
              before.heap_used == after.heap_used,
          "uncollected exit status retains no process memory");
    check(wait_task(pid) == 7, "exit status survives early memory reclamation");
    settle();

    char name[32], last[27];
    memset(name, 'a', 31);
    name[31] = 0;
    memset(last, 'b', 26);
    last[26] = 0;
    chdir_path("/tmp");
    bool made = true;
    for (u32 i = 0; i < 5; ++i)
        made = mkdir_path(name) == 0 && chdir_path(name) == 0 && made;
    int fd = open_file(last, NV_WRITE | NV_CREATE | NV_EXCL);
    check(made && fd >= 3, "maximum canonical file path remains usable");
    if (fd >= 3)
        close_file(fd);
    remove_path(last);
    made = mkdir_path(last) == 0 && chdir_path(last) == 0 && made;
    info(&before);
    int dir_result = mkdir_path("x");
    fd = open_file("y", NV_WRITE | NV_CREATE);
    info(&after);
    check(made && dir_result == -NV_E2BIG && fd == -NV_E2BIG && before.nodes == after.nodes,
          "overlong canonical paths rejected before creating nodes");
    if (fd >= 3)
        close_file(fd);
    remove_path("y");
    remove_path("x");
    chdir_path("..");
    remove_path(last);
    for (u32 i = 0; i < 5; ++i) {
        chdir_path("..");
        remove_path(name);
    }
    chdir_path("/home");
    int source = open_file("/tmp/core-move", NV_WRITE | NV_CREATE | NV_EXCL);
    if (source >= 0)
        close_file(source);
    check(source >= 0 && move_path("/tmp/core-move", "/tmp/core-dest/") == -NV_ENOTDIR,
          "rename cannot turn a regular file into a trailing-slash path");
    remove_path("/tmp/core-move");
    remove_path("/tmp/core-dest");
}
static void exec_tests(void) {
    check(call(NV_EXEC, 0x100000, (uptr) "", 0) == -NV_EFAULT,
          "exec rejects supervisor path pointer");
    check(call(NV_EXEC, (uptr) "/apps/pulse", 0x100000, 0) == -NV_EFAULT,
          "exec rejects supervisor argument pointer");
    check(exec_program("/tmp/missing-program", "") == -NV_ENOENT,
          "missing exec keeps the calling program alive");
    check(copy_file("/apps/pulse", "/tmp/exec-bad") == 0, "prepare exec rollback fixture");
    int fd = open_file("/tmp/exec-bad", NV_WRITE);
    u32 entry = 0x100000;
    seek_file(fd, 24, 0);
    emit(fd, &entry, 4);
    close_file(fd);
    struct nv_info before, after;
    info(&before);
    bool ok = true;
    for (u32 i = 0; i < 20; ++i)
        if (exec_program("/tmp/exec-bad", "unchanged") != -NV_ENOEXEC)
            ok = false;
    info(&after);
    check(ok && before.free_pages == after.free_pages && before.heap_used == after.heap_used,
          "failed exec after mapping rolls back without leaks");
    remove_path("/tmp/exec-bad");
    check(copy_file("/apps/relay", "/tmp/relay-image") == 0, "prepare successful exec fixture");
    info(&before);
    ok = true;
    for (u32 i = 0; i < 12; ++i) {
        int pid = spawn("/apps/relay", "begin");
        int result = pid > 0 ? wait_task(pid) : pid;
        if (result != 37) {
            print("Exec fixture exit: ");
            print_u32((u32)result);
            println("");
            ok = false;
        }
        char data[8] = {0};
        fd = open_file("/tmp/exec-handle", NV_READ);
        if (fd < 0 || take(fd, data, 7) != 7 || strcmp(data, "abcdone"))
            ok = false;
        if (fd >= 0)
            close_file(fd);
        remove_path("/tmp/exec-handle");
    }
    yield();
    info(&after);
    check(ok,
          "exec preserves PID, parent, children, cwd and file offsets; resets memory and screen");
    check(before.free_pages == after.free_pages && before.heap_used == after.heap_used,
          "12 exec replacement cycles reclaim old and final images");
    remove_path("/tmp/relay-image");
    info(&before);
    int pid = spawn("/apps/relay", "orphan");
    ok = pid > 0 && wait_task(pid) == 0;
    settle();
    nap(100);
    info(&after);
    check(ok && before.free_pages == after.free_pages && before.heap_used == after.heap_used &&
              before.tasks == after.tasks,
          "orphan exits release memory and process table slots");
}
static void hardware_tests(void) {
    struct nv_cpu_info cpu;
    struct nv_gpu_info gpu;
    struct nv_platform_info platform;
    check(cpu_info(&cpu) == 1 && cpu.version == 1 && cpu.bits == 8 * sizeof(uptr) &&
              cpu.online_cpus == 1 && (cpu.usable & NV_CPU_X87),
          "CPU identity and enabled state ABI");
    check(call(NV_HARDWARE, 0, 0, 0) == -NV_EINVAL &&
              call(NV_HARDWARE, NV_HW_CPU, 1, (uptr)&cpu) == -NV_EINVAL &&
              call(NV_HARDWARE, NV_HW_PLATFORM, 1, (uptr)&platform) == -NV_EINVAL &&
              gpu_info(NV_GPU_MAX, &gpu) == -NV_EINVAL,
          "hardware operations and indices bounded");
    check(call(NV_HARDWARE, NV_HW_CPU, 0, 0x100000) == -NV_EFAULT &&
              call(NV_HARDWARE, NV_HW_GPU, 0, 0x20000000) == -NV_EFAULT &&
              call(NV_HARDWARE, NV_HW_PLATFORM, 0, (uptr)hardware_tests) == -NV_EFAULT &&
              call(NV_HARDWARE, NV_HW_CPU, 0, (uptr)hardware_tests) == -NV_EFAULT &&
              call(NV_HARDWARE, NV_HW_GPU, 0, 0x7ffffff0) == -NV_EFAULT,
          "hardware output rejects kernel, MMIO, text and crossing buffers");
    check(platform_info(&platform) == 1 && (platform.flags & NV_PLATFORM_CF8) &&
              platform.config_bytes ==
                  ((platform.flags & NV_PLATFORM_ECAM) ? 4096u : 256u) &&
              (!(platform.flags & NV_PLATFORM_ECAM) ||
               ((platform.flags & (NV_PLATFORM_ACPI | NV_PLATFORM_MCFG)) ==
                    (NV_PLATFORM_ACPI | NV_PLATFORM_MCFG) &&
                platform.ecam_regions > 0)) &&
              !platform.reserved,
          "ACPI and PCI configuration method ABI is internally consistent");
    int n = gpu_info(0, &gpu);
    check(n == 0 || (n == 1 && gpu.class_code == 3 && gpu.state == NV_GPU_DISCOVERED &&
                     (!gpu.ext_capabilities || (platform.flags & NV_PLATFORM_ECAM))),
          "display discovery reports absence or a read-only PCI snapshot");
    int pid = spawn("/apps/vector", "");
    check(pid > 0 && wait_task(pid) == 0, "FP/SIMD isolation, preemption and exec reset");
    pid = spawn("/apps/fault", "sse");
    int status = pid > 0 ? wait_task(pid) : pid;
    if (status == 78)
        println("SKIP SSE #XM delivery: QEMU TCG records MXCSR flags without trapping");
    if (!(cpu.usable & NV_CPU_SSE))
        println("SKIP SSE #XM delivery: CPU has no enabled SSE");
    check(pid > 0 && ((cpu.usable & NV_CPU_SSE) ? (status == 147 || status == 78) : status == 77),
          "SSE exception outcome classified (see explicit SKIP if not exercised)");
    pid = spawn("/apps/fault", "avx");
    check(pid > 0 && wait_task(pid) == 134, "AVX is rejected until extended state saving exists");
}
/* The storage runner seeds this file in a checksummed snapshot before boot. */
static void restored_growth_tests(void) {
    const char *path = "/home/growth";
    struct nv_info before, after;
    info(&before);
    int fd = open_file(path, NV_READ | NV_WRITE | NV_APPEND);
    check(fd >= 3 && seek_file(fd, 0, 2) == NV_FILE_MAX - 1,
          "restored file has its exact saved length");
    if (fd < 0)
        return;
    check(emit(fd, "!", 1) == 1, "append reaches the file size limit");
    info(&after);
    check(after.heap_used == before.heap_used,
          "restored file growth stays within its aligned 128 KiB buffer budget");
    bool valid = seek_file(fd, 0, 0) == 0;
    u8 buffer[1024];
    for (u32 offset = 0; offset < NV_FILE_MAX; offset += sizeof(buffer)) {
        int n = take(fd, buffer, sizeof(buffer));
        if (n != (int)sizeof(buffer)) {
            valid = false;
            break;
        }
        for (u32 i = 0; i < sizeof(buffer); ++i)
            valid = valid && buffer[i] == (offset + i == NV_FILE_MAX - 1 ? '!' : 0x5a);
    }
    check(valid, "restored bytes and appended byte survive reallocation");
    check(emit(fd, "?", 1) == -NV_ENOSPC, "growth beyond the file limit is rejected");
    check(close_file(fd) == 0 && remove_path(path) == 0, "restored growth fixture removed");
    info(&after);
    check(after.heap_used + NV_FILE_MAX == before.heap_used,
          "removing the grown file returns its entire heap allocation");
}
int user_main(const char *args) {
    if (app_help("probe", args))
        return 0;
    println("Nuvora Core integration probe (running in Ring 3)");
    if (!strcmp(args, "devctl")) {
        devctl_tests();
    } else if (!strcmp(args, "file-growth")) {
        restored_growth_tests();
    } else {
        abi_tests();
        hardware_tests();
        devctl_tests();
        filesystem_tests();
        memory_tests();
        executable_tests();
        process_tests();
        core_regressions();
        exec_tests();
        tokenizer_tests();
    }
    print("PROBE RESULT: ");
    print_u32(passed);
    print(" passed, ");
    print_u32(failed);
    println(" failed");
    control(NV_CTL_TEST_EXIT, failed ? 1 : 0);
    return failed ? 1 : 0;
}
