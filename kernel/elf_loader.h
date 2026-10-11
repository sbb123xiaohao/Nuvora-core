#ifndef NV_ELF_LOADER_H
#define NV_ELF_LOADER_H
/* Private x86-64 executable loader; validation precedes all user mappings. */
struct elf_header {
    u8 ident[16];
    u16 type, machine;
    u32 version;
    u64 entry, phoff, shoff;
    u32 flags;
    u16 ehsize, phentsize, phnum, shentsize, shnum, shstr;
} PACKED;
struct program_header {
    u32 type, flags;
    u64 offset, vaddr, paddr, filesz, memsz, align;
} PACKED;
#define ELF_CLASS 2
#define ELF_MACHINE 62
_Static_assert(sizeof(struct elf_header) == 64, "ELF64 header");
_Static_assert(sizeof(struct program_header) == 56, "ELF64 program header");
struct elf_dynamic { i64 tag; u64 value; } PACKED;
struct elf_rela { u64 offset, info; i64 addend; } PACKED;
struct elf_relocations { const struct elf_rela *entries; u32 count; };
#define ELF_RELRO 0x6474e552u
#define ELF_STACK 0x6474e551u
static const struct program_header *elf_segment(const struct program_header *ph, u32 count,
                                                u64 address, u64 bytes, u32 flags, bool file) {
    for (u32 i=0;i<count;++i) {
        const struct program_header *p=&ph[i];
        u64 size=file?p->filesz:p->memsz;
        if (p->type==1 && (p->flags&flags)==flags && address>=p->vaddr &&
            address-p->vaddr<=size && bytes<=size-(address-p->vaddr)) return p;
    }
    return NULL;
}
static const u8 *elf_file_range(const u8 *data, const struct program_header *ph, u32 count,
                               u64 address, u64 bytes) {
    const struct program_header *p=elf_segment(ph,count,address,bytes,0,true);
    return p?data+p->offset+(address-p->vaddr):NULL;
}
static int elf_relocations(const u8 *data, const struct program_header *ph, u32 count,
                           struct elf_relocations *out) {
    const struct program_header *dynamic=NULL;
    for (u32 i=0;i<count;++i) if (ph[i].type==2) {
        if (dynamic) return -NV_ENOEXEC;
        dynamic=&ph[i];
    }
    *out=(struct elf_relocations){0};
    if (!dynamic) return 0; /* Pure position-independent code needs no fixups. */
    if (!dynamic->filesz || dynamic->filesz>65536 || dynamic->filesz%sizeof(struct elf_dynamic) ||
        dynamic->filesz!=dynamic->memsz) return -NV_ENOEXEC;
    const u8 *table=elf_file_range(data,ph,count,dynamic->vaddr,dynamic->filesz);
    if (!table || (u64)(table-data)!=dynamic->offset) return -NV_ENOEXEC;
    u64 address=0,bytes=0,entry_size=0,relative_count=0;
    u32 seen=0; bool ended=false;
    for (u64 at=0;at<dynamic->filesz;at+=sizeof(struct elf_dynamic)) {
        const struct elf_dynamic *d=(const void *)(table+at);
        if (!d->tag) { ended=true;break; }
        u32 bit=0;
        switch (d->tag) {
        case 7: bit=1;address=d->value;break; /* DT_RELA */
        case 8: bit=2;bytes=d->value;break;
        case 9: bit=4;entry_size=d->value;break;
        case 0x6ffffff9: bit=8;relative_count=d->value;break; /* DT_RELACOUNT */
        case 30: if (d->value&~8ull) return -NV_ENOEXEC;break; /* Only BIND_NOW. */
        case 0x6ffffffb: if (d->value&~0x08000001ull) return -NV_ENOEXEC;break; /* PIE/NOW. */
        /* Informational symbol/hash tables do not request symbol resolution. */
        case 4: case 5: case 6: case 10: case 11: case 21: case 0x6ffffef5: break;
        default: return -NV_ENOEXEC; /* Libraries, PLT, TLS, constructors, REL/RELR, text fixups. */
        }
        if (seen&bit) return -NV_ENOEXEC;
        seen|=bit;
    }
    if (!ended || ((seen&7) && (seen&7)!=7)) return -NV_ENOEXEC;
    if (!(seen&7)) return relative_count?-NV_ENOEXEC:0;
    if (entry_size!=sizeof(struct elf_rela) || bytes%entry_size || bytes/entry_size>65536 ||
        relative_count>bytes/entry_size) return -NV_ENOEXEC;
    if (!bytes) return 0;
    const struct elf_rela *rel=(const void *)elf_file_range(data,ph,count,address,bytes);
    if (!rel) return -NV_ENOEXEC;
    for (u32 i=0;i<bytes/entry_size;++i) {
        /* R_X86_64_RELATIVE with symbol zero, bounded writable destination,
           and a native image address. No dynamic lookup or executable writes. */
        if (rel[i].info!=8 || rel[i].addend<0 ||
            (u64)rel[i].addend>=NV_USER_IMAGE_END-NV_USER_IMAGE ||
            !elf_segment(ph,count,rel[i].offset,sizeof(u64),2,false)) return -NV_ENOEXEC;
    }
    out->entries=rel;out->count=(u32)(bytes/entry_size);
    return 0;
}
static int load_elf(struct task *t, const u8 *data, usize len, uptr *entry) {
    if (len < sizeof(struct elf_header))
        return -NV_ENOEXEC;
    const struct elf_header *h = (const void *)(uptr)data;
    if (memcmp(h->ident, "\177ELF", 4) || h->ident[4] != ELF_CLASS || h->ident[5] != 1 ||
        h->ident[6] != 1 || (h->type != 2 && h->type != 3) || h->machine != ELF_MACHINE || h->version != 1 ||
        h->ehsize != sizeof(*h) || h->phentsize != sizeof(struct program_header) || !h->phnum ||
        h->phnum > 32)
        return -NV_ENOEXEC;
    if (h->phoff > len || h->phnum > (len - h->phoff) / sizeof(struct program_header))
        return -NV_ENOEXEC;
    const struct program_header *ph = (const void *)(uptr)(data + h->phoff);
    bool pie=h->type==3;
    uptr bias=pie?NV_USER_IMAGE:0;
    bool executable_entry = false;
    u32 abi = 0;
    /* Reject the complete image before an invalid later segment or entry can
     * consume the physical budget while mapping an earlier large BSS. */
    for (u32 i = 0; i < h->phnum; ++i) {
        const struct program_header *p = &ph[i];
        if ((p->type == 2 && !pie) || p->type == 3 || p->type == 7 ||
            (p->type==ELF_STACK && (p->flags&1)))
            return -NV_ENOEXEC;
        if (p->type != 1)
            continue;
        if (!p->memsz && !p->filesz)
            continue;
        if (p->vaddr>NV_USER_IMAGE_END-bias) return -NV_ENOEXEC;
        uptr address=bias+p->vaddr;
        if (p->filesz > p->memsz || p->offset > len || p->filesz > len - p->offset ||
            address < USER_BASE || (address >= 0x80000000ull && address < NV_USER_IMAGE) || address >= (address < 0x80000000ull ? USER_IMAGE_END : NV_USER_IMAGE_END) ||
            p->memsz > (address < 0x80000000ull ? USER_IMAGE_END : NV_USER_IMAGE_END) - address || (p->flags & ~7u) || ((p->flags & 3) == 3))
            return -NV_ENOEXEC;
        if (p->align > 1 &&
            ((p->align & (p->align - 1)) || ((p->offset ^ address) & (p->align - 1))))
            return -NV_ENOEXEC;
        if (!p->memsz)
            continue;
        u32 segment_abi = address >= NV_USER_IMAGE ? NV_ABI_VERSION : NV_ABI_LEGACY;
        if (abi && abi != segment_abi) return -NV_ENOEXEC;
        abi = segment_abi;
        if ((p->flags & 1) && h->entry >= p->vaddr && h->entry - p->vaddr < p->memsz)
            executable_entry = true;
        uptr first = p->vaddr & ~(uptr)4095, end = ALIGN_UP(p->vaddr + p->memsz, PAGE);
        for (u32 j = 0; j < i; ++j) {
            if (ph[j].type != 1 || !ph[j].memsz) continue;
            uptr other_first = ph[j].vaddr & ~(uptr)4095;
            uptr other_end = ALIGN_UP(ph[j].vaddr + ph[j].memsz, PAGE);
            if (first < other_end && other_first < end) return -NV_ENOEXEC;
        }
    }
    if (!executable_entry)
        return -NV_ENOEXEC;
    struct elf_relocations relocations={0};
    if (pie && elf_relocations(data,ph,h->phnum,&relocations)<0) return -NV_ENOEXEC;
    for (u32 i=0;i<h->phnum;++i)
        if (ph[i].type==ELF_RELRO && ph[i].memsz &&
            !elf_segment(ph,h->phnum,ph[i].vaddr,ph[i].memsz,2,false)) return -NV_ENOEXEC;
    t->abi = abi;
    for (u32 i = 0; i < h->phnum; ++i) {
        const struct program_header *p = &ph[i];
        if (p->type != 1 || !p->memsz) continue;
        uptr first = (bias+p->vaddr) & ~(uptr)4095, end = ALIGN_UP(bias+p->vaddr + p->memsz, PAGE);
        for (uptr va = first; va < end; va += PAGE) {
            int r =
                vm_map(t->pd, va, ((p->flags & 2) ? P_WRITE : 0) | ((p->flags & 1) ? P_EXEC : 0));
            if (r < 0)
                return r == -NV_EEXIST ? -NV_ENOEXEC : r;
        }
        if (copy_to_space(t->pd, bias+p->vaddr, data + p->offset, p->filesz) < 0)
            return -NV_ENOEXEC;
    }
    for (u32 i=0;i<relocations.count;++i) {
        u64 value=bias+(u64)relocations.entries[i].addend;
        if (copy_to_space(t->pd,bias+relocations.entries[i].offset,&value,sizeof(value))<0)
            return -NV_ENOEXEC;
    }
    for (u32 i=0;i<h->phnum;++i) if (ph[i].type==ELF_RELRO && ph[i].memsz) {
        uptr first=(bias+ph[i].vaddr)&~(uptr)(PAGE-1);
        uptr end=(bias+ph[i].vaddr+ph[i].memsz)&~(uptr)(PAGE-1);
        for (uptr va=first;va<end;va+=PAGE)
            if (vm_readonly(t->pd,va)<0) return -NV_ENOEXEC;
    }
    *entry = bias+h->entry;
    return 0;
}
#endif
