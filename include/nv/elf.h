#ifndef NV_ELF_H
#define NV_ELF_H
#include <nv/types.h>
#define NV_ELF_SEGMENTS 32u
struct nv_elf_header {
    u8 ident[16]; u16 type, machine; u32 version;
    u64 entry, phoff, shoff; u32 flags;
    u16 ehsize, phentsize, phnum, shentsize, shnum, shstr;
} PACKED;
struct nv_elf_segment {
    u32 type, flags; u64 offset, vaddr, paddr, filesz, memsz, align;
} PACKED;
struct nv_elf_image {
    u32 entry, count, phnum, phdr;
    struct nv_elf_segment segments[NV_ELF_SEGMENTS];
};
int nv_elf64_validate(const void *, u32, u32, u32, struct nv_elf_image *);
/* Build a 4 KiB argc/argv/envp/auxv page and the legacy raw argument string. */
int nv_elf64_stack(const char *, const char *, const struct nv_elf_image *,
                   void *, u32, u32 *, u32 *);
#endif
