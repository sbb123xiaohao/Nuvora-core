#include "runtime.h"
bool app_help(const char *name, const char *args) {
    if (strcmp(args, "--help"))
        return false;
    static const struct {
        const char *name, *syntax, *purpose, *example;
    } apps[] = {
        {"loom", "forge loom", "Interactive command shell. Type help for every command.",
         "help ports"},
        {"folio", "folio [FILE]",
         "Full-screen text and formatted-document editor.\nCtrl-S saves; Ctrl-Q closes; F1 shows "
         "editor shortcuts.",
         "folio /home/report.nvd"},
        {"pulse", "forge pulse [quiet]",
         "Print five timed messages. quiet exits immediately with status 7.", "forge pulse"},
        {"spin", "scatter spin", "CPU-bound scheduler test; runs until terminated with quench PID.",
         "scatter spin"},
        {"fault", "forge fault [MODE]",
         "Trigger an intentional user-process fault to test isolation.\nModes: kernel, mmio, text, "
         "io, "
         "divide, opcode, fpu, sse, avx, guard, peer.\nNo mode triggers a null access; x64 also "
         "supports heap-exec and stack-exec.",
         "forge fault divide"},
        {"probe", "forge probe [devctl]",
         "Run integration assertions; creates temporary files and test processes.\nUse devctl to "
         "run only device-control regressions.",
         "forge probe"},
        {"relay", "forge relay [MODE]",
         "Kernel test fixtures. Modes: begin, orphan, pressure-test, fill.\nfill consumes memory "
         "until stopped; target is an internal exec fixture.",
         "forge relay begin"},
        {"vector", "forge vector",
         "Verify x87/MMX/SSE isolation across timer preemption, sleep, yield and exec.\nInternal "
         "worker modes: a, b, exec, clean.",
         "forge vector"}};
    for (u32 i = 0; i < ARRAY_LEN(apps); ++i) {
        if (strcmp(name, apps[i].name))
            continue;
        print(name);
        print(": ");
        println(apps[i].purpose);
        print("Usage: ");
        println(apps[i].syntax);
        print("Example: ");
        println(apps[i].example);
        print("Help: forge ");
        print(name);
        println(" --help");
        return true;
    }
    return false;
}
void print(const char *s) {
    usize n = strlen(s);
    while (n) {
        u32 part = MIN(n, 16384u);
        int r = emit(1, s, part);
        if (r <= 0)
            return;
        s += r;
        n -= (u32)r;
    }
}
void println(const char *s) {
    print(s);
    print("\n");
}
void print_u32(u32 n) {
    char b[32];
    number(b, n, 10);
    print(b);
}
void print_u64(u64 n) {
    char b[32];
    u32 i = sizeof(b) - 1;
    b[i] = 0;
    do { b[--i] = (char)('0' + n % 10); n /= 10; } while (n);
    print(b + i);
}
void print_hex(u32 n) {
    char b[32];
    number(b, n, 16);
    print("0x");
    print(b);
}
const char *error_name(int r) {
    static const char *const names[] = {"success",
                                        "invalid argument",
                                        "path not found",
                                        "out of memory",
                                        "invalid user buffer",
                                        "access denied",
                                        "already exists",
                                        "not a directory",
                                        "is a directory",
                                        "capacity reached",
                                        "object is in use",
                                        "invalid file handle",
                                        "directory is not empty",
                                        "unsupported executable",
                                        "not an unwaited child",
                                        "operation not implemented",
                                        "device I/O or format error",
                                        "device not available",
                                        "argument too long",
                                        "input not ready"};
    u32 n = r < 0 ? 0u - (u32)r : (u32)r;
    return n < ARRAY_LEN(names) ? names[n] : "unknown error";
}
void report_error(const char *action, int code) {
    print(action);
    print(": ");
    println(code == -NV_ENODEV && !strcmp(action, "anchor") ? "no Nuvora data disk"
                                                            : error_name(code));
}
int read_line(char *out, u32 cap) {
    if (cap < 2)
        return -NV_EINVAL;
    u32 n = 0;
    bool overflow = false;
    u32 escape = 0;
    for (;;) {
        char c;
        int r = take(0, &c, 1);
        if (r == -NV_EAGAIN) {
            nap(10);
            continue;
        }
        if (r < 0)
            return r;
        if (!r)
            continue;
        /* Ignore terminal cursor-key sequences; the VGA console uses PS/2 set 1. */
        if (c == 27) {
            escape = 1;
            continue;
        }
        if (escape) {
            if (escape == 1 && c == '[') {
                escape = 2;
                continue;
            }
            if (c >= '@' && c <= '~')
                escape = 0;
            continue;
        }
        if (c == '\r')
            continue;
        if (c == '\n') {
            print("\n");
            out[n] = 0;
            return overflow ? -NV_E2BIG : (int)n;
        }
        if (c == 3) {
            out[0] = 0;
            println("^C");
            return 0;
        }
        if (c == '\b' || c == 127) {
            if (n) {
                --n;
                print("\b \b");
            }
            continue;
        }
        if ((u8)c < 32 || (u8)c > 126)
            continue;
        if (n + 1 < cap) {
            out[n++] = c;
            emit(1, &c, 1);
        } else
            overflow = true;
    }
}
int tokenize(char *line, char **argv, u32 max) {
    char *r = line, *w = line;
    u32 n = 0;
    while (*r) {
        while (*r == ' ' || *r == '\t')
            ++r;
        if (!*r)
            break;
        if (n == max)
            return -NV_E2BIG;
        argv[n++] = w;
        char quote = 0;
        while (*r) {
            char c = *r++;
            if (c == '\\' && quote != '\'') {
                if (!*r)
                    return -NV_EINVAL;
                *w++ = *r++;
                continue;
            }
            if (quote) {
                if (c == quote)
                    quote = 0;
                else
                    *w++ = c;
                continue;
            }
            if (c == '\'' || c == '"') {
                quote = c;
                continue;
            }
            if (c == ' ' || c == '\t')
                break;
            *w++ = c;
        }
        if (quote)
            return -NV_EINVAL;
        *w++ = 0;
    }
    return (int)n;
}
int join_args(char *out, u32 cap, char **argv, int first, int argc) {
    u32 p = 0;
    for (int i = first; i < argc; ++i) {
        u32 n = strlen(argv[i]);
        if (n + (i > first ? 1u : 0u) >= cap - p)
            return -NV_E2BIG;
        if (i > first)
            out[p++] = ' ';
        memcpy(out + p, argv[i], n);
        p += n;
    }
    out[p] = 0;
    return 0;
}
int copy_file(const char *from, const char *to) {
    /* Existing destinations are deliberately refused, including the source itself. */
    int existing = open_file(to, NV_READ);
    if (existing >= 0) {
        close_file(existing);
        return -NV_EEXIST;
    }
    if (existing != -NV_ENOENT)
        return existing;
    int src = open_file(from, NV_READ);
    if (src < 0)
        return src;
    int dst = open_file(to, NV_WRITE | NV_CREATE | NV_EXCL);
    if (dst < 0) {
        close_file(src);
        return dst;
    }
    char buf[1024];
    int result = 0;
    for (;;) {
        int n = take(src, buf, sizeof(buf));
        if (n < 0) {
            result = n;
            break;
        }
        if (!n)
            break;
        int w = emit(dst, buf, (u32)n);
        if (w != n) {
            result = w < 0 ? w : -NV_EIO;
            break;
        }
    }
    close_file(src);
    close_file(dst);
    if (result < 0)
        remove_path(to);
    return result;
}
