#include "runtime.h"
#include "commands.h"
#include "ports.h"
#include "hardware.h"
static void status(void) {
    struct nv_info i;
    if (info(&i) < 0)
        return;
    print("RAM managed: ");
    print_u32(i.ram_pages * 4);
    print(" KiB; free: ");
    print_u32(i.free_pages * 4);
    println(" KiB");
    print("Kernel heap: ");
    print_u32(i.heap_used);
    print(" / ");
    print_u32(i.heap_total);
    println(" bytes");
    print("Tasks: ");
    print_u32(i.tasks);
    print("; filesystem nodes: ");
    print_u32(i.nodes);
    print("; uptime: ");
    print_u32(i.ticks / i.hz);
    println(" s");
    if (i.disk_present) {
        print("Data disk ready; committed generation: ");
        print_u32(i.saved_generation);
        print("\n");
    } else
        println("No Nuvora data disk. Files are held in RAM.");
}
static int show_file(const char *path) {
    int fd = open_file(path, NV_READ);
    if (fd < 0)
        return fd;
    char buf[512];
    int result = 0;
    for (;;) {
        int n = take(fd, buf, sizeof(buf));
        if (n <= 0) {
            result = n;
            break;
        }
        emit(1, buf, (u32)n);
    }
    close_file(fd);
    return result;
}
static void process_list(void) {
    println("PID  PARENT  STATE  TICKS  PAGES  PROGRAM");
    static const char *const state[] = {"unused", "ready", "sleep", "wait", "done"};
    for (u32 n = 0; n < NV_TASK_MAX; ++n) {
        struct nv_taskinfo t;
        if (task_at(n, &t) <= 0)
            continue;
        print_u32(t.pid);
        print("  ");
        print_u32(t.parent);
        print("  ");
        print(state[t.state]);
        print("  ");
        print_u32(t.cpu_ticks);
        print("  ");
        print_u32(t.pages);
        print("  ");
        println(t.name);
    }
}
static int run_app(const char *app, char **argv, int argc, bool background, bool standard) {
    char path[NV_PATH_MAX], args[NV_ARG_MAX];
    bool slash = false;
    for (const char *p = app; *p; ++p)
        if (*p == '/')
            slash = true;
    if (slash) {
        if (strlcpy(path, app, sizeof(path)) >= sizeof(path))
            return -NV_E2BIG;
    } else {
        strlcpy(path, "/apps/", sizeof(path));
        if (strlen(app) + 6 >= sizeof(path))
            return -NV_E2BIG;
        strlcpy(path + 6, app, sizeof(path) - 6);
    }
    int r = 0;
    if (!standard) r = join_args(args, sizeof(args), argv, 2, argc);
    else {
        u32 n = 0;
        for (int i = 2; i < argc; ++i) {
            if (n + 3 >= sizeof(args)) return -NV_E2BIG;
            if (i > 2) args[n++] = ' ';
            args[n++] = '"';
            for (const char *p = argv[i]; *p; ++p) {
                if (n + 3 >= sizeof(args)) return -NV_E2BIG;
                if (*p == '"' || *p == '\\') args[n++] = '\\';
                args[n++] = *p;
            }
            args[n++] = '"';
        }
        args[n] = 0;
    }
    if (r < 0)
        return r;
    int pid = spawn(path, args);
    if (pid < 0)
        return pid;
    if (background) {
        print("Started pid ");
        print_u32((u32)pid);
        print("\n");
        return 0;
    }
    int code = wait_task(pid);
    if (code < 0)
        return code;
    print("Process ");
    print_u32((u32)pid);
    print(" exited ");
    print_u32((u32)code);
    print("\n");
    return 0;
}
static int dispatch(int n, char **v) {
    const char *cmd = v[0];
    if (!strcmp(cmd, "run") && n >= 2) return run_app(v[1], v, n, false, true);
    const struct command_help *entry = find_command(cmd);
    if (n == 2 && !strcmp(v[1], "--help") && entry) {
        explain_command(entry);
        return 0;
    }
    if ((!strcmp(cmd, "help") || !strcmp(cmd, "atlas")) && n <= 2) {
        if (n == 1)
            help();
        else {
            entry = find_command(v[1]);
            if (entry)
                explain_command(entry);
            else
                println("Unknown command. Type help for the command list.");
        }
        return 0;
    }
    if (!strcmp(cmd, "silicon") && n == 1)
        return show_cpu();
    if (!strcmp(cmd, "firmament") && n == 1)
        return show_platform();
    if (!strcmp(cmd, "prism") && n == 1)
        return show_gpu();
    if (!strcmp(cmd, "ports") && (n == 1 || (n == 2 && !strcmp(v[1], "--scan")))) {
        int r = n == 2 ? usb_scan() : 0;
        return r < 0 ? r : show_ports();
    }
    if (!strcmp(cmd, "folio") && n <= 2) {
        char a[NV_ARG_MAX] = {0};
        if (n == 2) {
            /* The program receives the path verbatim, including spaces. */
            if (strlcpy(a, v[1], sizeof(a)) >= sizeof(a))
                return -NV_E2BIG;
        }
        int pid = spawn("/apps/folio", a);
        return pid < 0 ? pid : (wait_task(pid) < 0 ? -NV_ECHILD : 0);
    }
    if (!strcmp(cmd, "origin") && n == 1)
        return show_file("/sys/version");
    if (!strcmp(cmd, "horizon") && n == 1) {
        status();
        return 0;
    }
    if (!strcmp(cmd, "where") && n == 1) {
        char cwd[NV_PATH_MAX];
        int r = getcwd_path(cwd, sizeof(cwd));
        if (!r)
            println(cwd);
        return r;
    }
    if (!strcmp(cmd, "step") && n == 2)
        return chdir_path(v[1]);
    if (!strcmp(cmd, "glance") && n <= 2) {
        const char *path = n == 2 ? v[1] : ".";
        struct nv_dirent e;
        for (u32 i = 0;; ++i) {
            int r = list_dir(path, i, &e);
            if (r <= 0)
                return r;
            print(e.kind == NV_DIR      ? "dir   "
                  : e.kind == NV_DEVICE ? "dev   "
                  : e.kind == NV_PROC   ? "live  "
                                        : "file  ");
            print(e.name);
            if (e.kind == NV_FILE) {
                print("  ");
                print_u32(e.size);
                print(" B");
            }
            print("\n");
        }
    }
    if (!strcmp(cmd, "nest") && n == 2)
        return mkdir_path(v[1]);
    if ((!strcmp(cmd, "weave") || !strcmp(cmd, "stitch")) && n >= 3) {
        char text[512];
        int r = join_args(text, sizeof(text) - 1, v, 2, n);
        if (r < 0)
            return r;
        u32 len = strlen(text);
        text[len++] = '\n';
        text[len] = 0;
        u32 flags = NV_WRITE | NV_CREATE | (!strcmp(cmd, "stitch") ? NV_APPEND : NV_TRUNC);
        int fd = open_file(v[1], flags);
        if (fd < 0)
            return fd;
        r = emit(fd, text, len);
        close_file(fd);
        return r < 0 ? r : 0;
    }
    if (!strcmp(cmd, "unfold") && n == 2)
        return show_file(v[1]);
    if (!strcmp(cmd, "mirror") && n == 3)
        return copy_file(v[1], v[2]);
    if (!strcmp(cmd, "shift") && n == 3)
        return move_path(v[1], v[2]);
    if (!strcmp(cmd, "prune") && n == 2)
        return remove_path(v[1]);
    if (!strcmp(cmd, "sparks") && n == 1) {
        process_list();
        return 0;
    }
    if ((!strcmp(cmd, "forge") || !strcmp(cmd, "scatter")) && n >= 2)
        return run_app(v[1], v, n, !strcmp(cmd, "scatter"), false);
    if (!strcmp(cmd, "trial") && n == 1) {
        char *args[] = {"forge", "probe"};
        return run_app("probe", args, 2, false, false);
    }
    if ((!strcmp(cmd, "gather") || !strcmp(cmd, "quench") || !strcmp(cmd, "doze")) && n == 2) {
        u32 value;
        if (parse_u32(v[1], &value) < 0 || value > 0x7fffffff)
            return -NV_EINVAL;
        if (!strcmp(cmd, "doze"))
            return nap(value);
        if (!strcmp(cmd, "quench"))
            return stop_task((int)value);
        int r = wait_task((int)value);
        if (r >= 0) {
            print("Exit status: ");
            print_u32((u32)r);
            print("\n");
            return 0;
        }
        return r;
    }
    if (!strcmp(cmd, "tempo") && n == 1) {
        print_u32(clock_ticks());
        println(" ticks @ 100 Hz");
        return 0;
    }
    if (!strcmp(cmd, "anchor") && n == 1) {
        int r = control(NV_CTL_SYNC, 0);
        if (!r)
            println("Saved /home.");
        return r;
    }
    if (!strcmp(cmd, "scrub") && n == 1)
        return control(NV_CTL_CLEAR, 0);
    if (!strcmp(cmd, "rest") && n == 1)
        return control(NV_CTL_POWEROFF, 0);
    if (!strcmp(cmd, "renew") && n == 1)
        return control(NV_CTL_REBOOT, 0);
    if (entry) {
        println("Invalid arguments.");
        explain_command(entry);
    } else
        println("Unknown command. Type help for the command list.");
    return 0;
}
int user_main(const char *args) {
    if (app_help("loom", args))
        return 0;
    println("Loom / Nuvora command environment");
    println("Type help for all commands; NAME --help explains one command.");
    char line[512], cwd[NV_PATH_MAX];
    char *argv[24];
    for (;;) {
        if (getcwd_path(cwd, sizeof(cwd)) < 0)
            strlcpy(cwd, "?", sizeof(cwd));
        print("\n");
        print(cwd);
        print(" :: ");
        int n = read_line(line, sizeof(line));
        if (n < 0) {
            report_error("Input discarded", n);
            continue;
        }
        if (!n)
            continue;
        n = tokenize(line, argv, ARRAY_LEN(argv));
        if (n < 0) {
            report_error("Syntax", n);
            continue;
        }
        if (!n)
            continue;
        int result = dispatch(n, argv);
        if (result < 0)
            report_error(argv[0], result);
    }
}
