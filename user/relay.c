#include "runtime.h"
/* Integration fixtures for exec, orphan cleanup and memory exhaustion. */
static volatile u32 image_marker;
static bool find_named(const char *name, struct nv_taskinfo *out) {
    for (u32 i = 0; i < NV_TASK_MAX; ++i)
        if (task_at(i, out) > 0 && !strcmp(out->name, name))
            return true;
    return false;
}
static int replace_begin(void) {
    struct nv_taskinfo self;
    if (!find_named("relay", &self))
        return 91;
    int fd = open_file("/tmp/exec-handle", NV_READ | NV_WRITE | NV_CREATE | NV_EXCL);
    if (fd < 3 || emit(fd, "abc", 3) != 3 || seek_file(fd, 1, 0) != 1)
        return 92;
    u8 *heap = grow(3);
    if ((iptr)heap < 0 || chdir_path("/tmp") < 0)
        return 93;
    heap[0] = 0x5a;
    image_marker = 0x11223344;
    int child = spawn("/apps/pulse", "quiet");
    if (child < 0 || surface(NV_SCREEN_ACQUIRE, NULL) < 0)
        return 94;
    char pid[16], parent[16], handle[16], descendant[16], args[NV_ARG_MAX];
    number(pid, self.pid, 10);
    number(parent, self.parent, 10);
    number(handle, (u32)fd, 10);
    number(descendant, (u32)child, 10);
    char *parts[] = {"target", pid, parent, handle, descendant};
    if (join_args(args, sizeof(args), parts, 0, ARRAY_LEN(parts)) < 0)
        return 95;
    exec_program("./relay-image", args);
    return 96; /* A successful exec cannot return here. */
}
static int replace_target(const char *args) {
    char line[NV_ARG_MAX], *parts[5], cwd[NV_PATH_MAX];
    strlcpy(line, args, sizeof(line));
    u32 pid, parent, fd, child;
    if (tokenize(line, parts, ARRAY_LEN(parts)) != 5 || parse_u32(parts[1], &pid) < 0 ||
        parse_u32(parts[2], &parent) < 0 || parse_u32(parts[3], &fd) < 0 ||
        parse_u32(parts[4], &child) < 0)
        return 81;
    struct nv_taskinfo self;
    if (!find_named("relay-image", &self) || self.pid != pid || self.parent != parent)
        return 82;
    if (getcwd_path(cwd, sizeof(cwd)) < 0 || strcmp(cwd, "/tmp") || image_marker ||
        (uptr)grow(0) != 0x50000000u || call(NV_EMIT, 1, 0x50000000u, 1) != -NV_EFAULT)
        return 83;
    char data[3] = {0};
    if (take((int)fd, data, 2) != 2 || strcmp(data, "bc") || emit((int)fd, "done", 4) != 4 ||
        close_file((int)fd) < 0 || wait_task((int)child) != 7)
        return 84;
    if (key_event() != -NV_EACCESS || surface(NV_SCREEN_ACQUIRE, NULL) < 0 ||
        surface(NV_SCREEN_RELEASE, NULL) < 0)
        return 85;
    return 37;
}
static int fill_memory(void) {
    for (u32 i = 0; i < 1024; ++i) {
        u8 *p = grow(1);
        if ((iptr)p < 0)
            break;
        p[0] = 0xa5;
        p[NV_PAGE - 1] = 0x5a;
    }
    int fd = open_file("/tmp/pressure-ready", NV_WRITE | NV_APPEND);
    if (fd < 0 || emit(fd, "R", 1) != 1 || close_file(fd) < 0)
        return 71;
    for (;;)
        nap(100);
}
static int pressure_test(void) {
    struct nv_info before, after;
    info(&before);
    int ready = open_file("/tmp/pressure-ready", NV_READ | NV_WRITE | NV_CREATE | NV_EXCL);
    if (ready < 0)
        return 61;
    u8 *initial = grow(1);
    if ((iptr)initial < 0)
        return 62;
    initial[0] = 0x5a;
    u32 grown = 1;
    int children[NV_TASK_MAX], count = 0;
    bool ok = true;
    for (u32 i = 0; i < NV_TASK_MAX - 2; ++i) {
        int pid = spawn("/apps/relay", "fill");
        if (pid < 0) {
            ok = pid == -NV_ENOMEM;
            break;
        }
        children[count++] = pid;
        u32 start = clock_ticks();
        while (seek_file(ready, 0, 2) < count && clock_ticks() - start < 500)
            nap(10);
        if (seek_file(ready, 0, 2) != count) {
            ok = false;
            break;
        }
        info(&after);
        if (after.free_pages < 4)
            break;
    }
    while (grown < 1024 && (iptr)grow(1) > 0)
        ++grown;
    info(&after);
    u32 remaining = after.free_pages;
    for (u32 i = 0; i < 8; ++i) {
        if (exec_program("/apps/pulse", "quiet") != -NV_ENOMEM ||
            spawn("/apps/pulse", "quiet") != -NV_ENOMEM || (iptr)grow(1) != -NV_ENOMEM ||
            initial[0] != 0x5a)
            ok = false;
        info(&after);
        if (after.free_pages != remaining)
            ok = false;
    }
    for (int i = 0; i < count; ++i)
        if (stop_task(children[i]) < 0 || wait_task(children[i]) != 143)
            ok = false;
    if ((iptr)grow(-(i32)grown) < 0)
        ok = false;
    close_file(ready);
    remove_path("/tmp/pressure-ready");
    yield();
    info(&after);
    ok = ok && count > 0 && before.free_pages == after.free_pages &&
         before.heap_used == after.heap_used && before.tasks == after.tasks &&
         before.nodes == after.nodes;
    int pid = spawn("/apps/pulse", "quiet");
    ok = pid > 0 && wait_task(pid) == 7 && ok;
    println(ok ? "PRESSURE RESULT: PASS; exec/spawn/grow rollback and full recovery"
               : "PRESSURE RESULT: FAIL");
    return ok ? 0 : 1;
}
int user_main(const char *args) {
    if (app_help("relay", args))
        return 0;
    if (!strcmp(args, "begin"))
        return replace_begin();
    if (!strncmp(args, "target ", 7))
        return replace_target(args);
    if (!strcmp(args, "orphan"))
        return spawn("/apps/pulse", "quiet") > 0 ? 0 : 1;
    if (!strcmp(args, "fill"))
        return fill_memory();
    if (!strcmp(args, "pressure-test"))
        return pressure_test();
    println("Relay kernel fixtures: begin, orphan, pressure-test");
    return 0;
}
