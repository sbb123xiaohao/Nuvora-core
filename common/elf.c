#include <nv/elf.h>
#include <nv/abi.h>
#include <nv/string.h>
_Static_assert(sizeof(struct nv_elf_header) == 64, "ELF64 header");
_Static_assert(sizeof(struct nv_elf_segment) == 56, "ELF64 program header");
int nv_elf64_validate(const void *bytes, u32 length, u32 low, u32 high,
                      struct nv_elf_image *out) {
    if (!bytes || !out || length < sizeof(struct nv_elf_header) || low >= high ||
        ((low | high) & 4095u)) return -NV_ENOEXEC;
    const u8 *data = bytes;
    struct nv_elf_header h;
    memcpy(&h, data, sizeof(h));
    if (memcmp(h.ident, "\177ELF", 4) || h.ident[4] != 2 || h.ident[5] != 1 ||
        h.ident[6] != 1 || h.type != 2 || h.machine != 62 || h.version != 1 ||
        h.ehsize != sizeof(h) || h.phentsize != sizeof(struct nv_elf_segment) ||
        !h.phnum || h.phnum > NV_ELF_SEGMENTS || h.phoff > length ||
        h.phnum > (length - h.phoff) / sizeof(struct nv_elf_segment)) return -NV_ENOEXEC;
    memset(out, 0, sizeof(*out));
    out->phnum = h.phnum;
    bool entry = false;
    for (u32 i = 0; i < h.phnum; ++i) {
        struct nv_elf_segment p;
        memcpy(&p, data + h.phoff + i * sizeof(p), sizeof(p));
        if (p.type == 2 || p.type == 3 || p.type == 7 ||
            (p.type == 0x6474e551u && (p.flags & 1))) return -NV_ENOEXEC;
        if (p.type != 1 || (!p.filesz && !p.memsz)) continue;
        if (p.filesz > p.memsz || p.offset > length || p.filesz > length - p.offset ||
            p.vaddr < low || p.vaddr >= high || p.memsz > high - p.vaddr ||
            (p.flags & ~7u) || (p.flags & 3) == 3 ||
            (p.align > 1 && ((p.align & (p.align - 1)) ||
                             ((p.offset ^ p.vaddr) & (p.align - 1))))) return -NV_ENOEXEC;
        u64 first = p.vaddr & ~4095ull, end = ALIGN_UP(p.vaddr + p.memsz, 4096);
        for (u32 j = 0; j < out->count; ++j) {
            const struct nv_elf_segment *q = &out->segments[j];
            if (first < ALIGN_UP(q->vaddr + q->memsz, 4096) &&
                (q->vaddr & ~4095ull) < end) return -NV_ENOEXEC;
        }
        /* An entry in executable BSS cannot contain an instruction image. */
        if ((p.flags & 1) && h.entry >= p.vaddr && h.entry - p.vaddr < p.filesz) entry = true;
        if (h.phoff >= p.offset && h.phoff - p.offset <= p.filesz &&
            (u64)h.phnum * sizeof(p) <= p.filesz - (h.phoff - p.offset))
            out->phdr = (u32)(p.vaddr + h.phoff - p.offset);
        out->segments[out->count++] = p;
    }
    if (!entry) return -NV_ENOEXEC;
    out->entry = (u32)h.entry;
    return 0;
}
int nv_elf64_stack(const char *path, const char *args, const struct nv_elf_image *elf,
                   void *page, u32 base, u32 *sp, u32 *raw) {
    usize pn = strnlen(path, NV_PATH_MAX), an = strnlen(args, NV_ARG_MAX);
    if (pn == NV_PATH_MAX || an == NV_ARG_MAX) return -NV_E2BIG;
    u8 *data = page;
    memset(data, 0, 4096);
    u64 argv[32]; u32 argc = 1, pos = 2048;
    argv[0] = base + pos;
    memcpy(data + pos, path, pn + 1); pos += (u32)pn + 1;
    *raw = base + pos;
    memcpy(data + pos, args, an + 1); pos += (u32)an + 1;
    for (u32 i = 0; i < an;) {
        while (args[i] == ' ' || args[i] == '\t') ++i;
        if (!args[i]) break;
        if (argc == ARRAY_LEN(argv)) return -NV_E2BIG;
        argv[argc++] = base + pos;
        char quote = 0;
        while (args[i]) {
            char c = args[i++];
            if (c == '\\' && quote != '\'') {
                if (!args[i]) return -NV_EINVAL;
                data[pos++] = (u8)args[i++];
            } else if (quote) {
                if (c == quote) quote = 0;
                else data[pos++] = (u8)c;
            } else if (c == '\'' || c == '"') quote = c;
            else if (c == ' ' || c == '\t') break;
            else data[pos++] = (u8)c;
        }
        if (quote) return -NV_EINVAL;
        data[pos++] = 0;
    }
    u64 words[64]; u32 n = 0;
    words[n++] = argc;
    for (u32 i = 0; i < argc; ++i) words[n++] = argv[i];
    words[n++] = 0; words[n++] = 0; /* argv end, empty environment */
    words[n++] = 6; words[n++] = 4096; /* AT_PAGESZ */
    words[n++] = 9; words[n++] = elf->entry;
    if (elf->phdr) {
        words[n++] = 3; words[n++] = elf->phdr;
        words[n++] = 4; words[n++] = sizeof(struct nv_elf_segment);
        words[n++] = 5; words[n++] = elf->phnum;
    }
    words[n++] = 0; words[n++] = 0;
    u32 offset = (2048 - n * 8) & ~15u;
    memcpy(data + offset, words, n * 8); *sp = base + offset;
    return 0;
}
