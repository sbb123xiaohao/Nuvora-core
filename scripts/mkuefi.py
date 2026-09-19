#!/usr/bin/env python3
"""Turn the fully linked UEFI stub ELF64 into a PE32+ EFI application.

The stub prefers 0x02000000; absolute ELF relocations become a PE relocation
table so firmware may load it elsewhere. The stub uses the large code model
and full-width relocations. Section bytes and the entry come from the ELF.
"""
import pathlib
import struct
import sys

IMAGE_BASE = 0x02000000
SECTION_ALIGN = 4096
FILE_ALIGN = 512

SHT_NOBITS = 8
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4
SHF_WRITE = 0x1

CHAR_CODE = 0x60000020  # CNT_CODE | MEM_EXECUTE | MEM_READ
CHAR_DATA = 0xC0000040  # CNT_INITIALIZED_DATA | MEM_READ | MEM_WRITE
CHAR_RODATA = 0x40000040  # CNT_INITIALIZED_DATA | MEM_READ
CHAR_BSS = 0xC0000080  # CNT_UNINITIALIZED_DATA | MEM_READ | MEM_WRITE
CHAR_RELOC = 0x42000040  # CNT_INITIALIZED_DATA | MEM_DISCARDABLE | MEM_READ

# ELF -> PE base-relocation type mapping. Only absolute relocations need
# base fixups; PC-relative ones are position independent by nature.
RELOC_MAP = {1: 10, 10: 3, 11: 3}  # R_X86_64_64->DIR64, 32/32S->HIGHLOW
R_X86_64_64, R_X86_64_32, R_X86_64_32S = 1, 10, 11

KEEP = (".text", ".rodata", ".data", ".bss")


def align_up(value: int, boundary: int) -> int:
    return (value + boundary - 1) & ~(boundary - 1)


def main() -> None:
    elf_path = pathlib.Path(sys.argv[1])
    out_path = pathlib.Path(sys.argv[2])
    data = elf_path.read_bytes()
    assert data[:4] == b"\x7fELF" and data[4] == 2 and data[5] == 1, "expected little-endian ELF64"
    machine, = struct.unpack_from("<H", data, 18)
    assert machine == 62, "expected x86-64 ELF"
    entry, phoff, shoff = struct.unpack_from("<QQQ", data, 24)
    phentsize, phnum = struct.unpack_from("<HH", data, 54)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 58)
    assert phentsize == 56 and shentsize == 64 and shnum > 0, "unexpected ELF header sizes"

    # Program headers must cover every kept section at its linked address.
    loads = []
    for i in range(phnum):
        p_type, _flags, p_offset, _vaddr, p_paddr, p_filesz, p_memsz = struct.unpack_from(
            "<IIQQQQQ", data, phoff + i * phentsize)
        if p_type == 1 and p_memsz:  # PT_LOAD
            loads.append((p_paddr, p_offset, p_filesz, p_memsz))
    assert loads, "no PT_LOAD segments"
    image_size = align_up(max(paddr + memsz for paddr, _, _, memsz in loads) - IMAGE_BASE,
                          SECTION_ALIGN)

    shstr_off = struct.unpack_from("<Q", data, shoff + shstrndx * shentsize + 24)[0]
    sections = {}
    for i in range(shnum):
        base = shoff + i * shentsize
        name_off, sh_type, sh_flags, sh_addr = struct.unpack_from("<IIQQ", data, base)
        sh_offset, sh_size = struct.unpack_from("<QQ", data, base + 24)
        if not sh_flags & SHF_ALLOC:
            continue
        end = data.index(b"\0", shstr_off + name_off)
        name = data[shstr_off + name_off:end].decode()
        if name in KEEP:
            sections[name] = (sh_type, sh_flags, sh_addr, sh_offset, sh_size)
    missing = [n for n in (".text", ".rodata", ".bss") if n not in sections]
    assert not missing, f"stub ELF lacks sections: {missing}"
    for name in KEEP:
        if name not in sections:
            continue
        kind, flags, addr, _, size = sections[name]
        if not size:
            continue
        covered = any(base <= addr and addr + size <= base + memsz
                      for base, _, _, memsz in loads)
        assert covered, f"section {name} is not covered by a PT_LOAD segment"
        if kind == SHT_NOBITS:
            assert addr % SECTION_ALIGN == 0, f"{name} not section aligned"

    def section_bytes(name: str) -> bytes:
        kind, flags, addr, offset, size = sections[name]
        if not size or kind == SHT_NOBITS:
            return b""
        return data[offset:offset + size]

    # PE section table ordered by virtual address.
    pe_sections = []
    for name in (".text", ".rodata", ".data", ".bss"):
        if name not in sections:
            continue
        kind, flags, addr, _, size = sections[name]
        if not size:
            continue
        characteristics = (CHAR_CODE if flags & SHF_EXECINSTR else
                           CHAR_DATA if flags & SHF_WRITE else CHAR_RODATA)
        if kind == SHT_NOBITS:
            characteristics = CHAR_BSS
        pe_sections.append([name, addr - IMAGE_BASE, size, characteristics])

    # Build a PE .reloc section from the absolute relocations preserved by
    # `ld --emit-relocs`, so firmware can load the image at any base.
    reloc_rvas = []
    for i in range(shnum):
        base = shoff + i * shentsize
        sh_type, = struct.unpack_from("<I", data, base + 4)
        if sh_type != 4:  # SHT_RELA
            continue
        target, = struct.unpack_from("<I", data, base + 44)
        assert target < shnum, "invalid relocation target"
        target_flags, = struct.unpack_from("<Q", data, shoff + target * shentsize + 8)
        if not target_flags & SHF_ALLOC:
            continue
        sh_offset, sh_size = struct.unpack_from("<QQ", data, base + 24)
        sh_entsize, = struct.unpack_from("<Q", data, base + 56)
        assert sh_entsize == 24, "unexpected rela entry size"
        for e in range(sh_size // 24):
            r_offset, r_info, _addend = struct.unpack_from("<QQq", data, sh_offset + e * 24)
            pe_type = RELOC_MAP.get(r_info & 0xffffffff)
            if pe_type is None:
                assert (r_info & 0xffffffff) in (2, 4, 24), "unsupported ELF relocation"
                continue
            assert pe_type == 10, "absolute 32-bit pointer: compile the EFI stub with -mcmodel=large"
            assert r_offset >= IMAGE_BASE, "absolute relocation below the image base"
            reloc_rvas.append((r_offset - IMAGE_BASE, pe_type))
    reloc_blob = b""
    reloc_rva = 0
    if reloc_rvas:
        pages = {}
        for rva, pe_type in sorted(reloc_rvas):
            pages.setdefault(rva >> 12, []).append((rva, pe_type))
        blocks = bytearray()
        for page in sorted(pages):
            entries = bytearray()
            for rva, pe_type in pages[page]:
                entries += struct.pack("<H", (pe_type << 12) | (rva & 0xfff))
            if len(entries) % 4:
                entries += struct.pack("<H", 0)  # IMAGE_REL_BASED_ABSOLUTE padding
            blocks += struct.pack("<II", page << 12, 8 + len(entries)) + entries
        reloc_blob = bytes(blocks)
        reloc_rva = align_up(image_size, SECTION_ALIGN)
        image_size = align_up(reloc_rva + len(reloc_blob), SECTION_ALIGN)
        pe_sections.append([".reloc", reloc_rva, len(reloc_blob), CHAR_RELOC])

    header_size = align_up(0x80 + 4 + 20 + 240 + 40 * len(pe_sections), FILE_ALIGN)
    raw_offset = header_size
    for sec in pe_sections:
        sec.append(raw_offset)  # pointer to raw data
        blob = reloc_blob if sec[0] == ".reloc" else section_bytes(sec[0])
        sec.append(align_up(len(blob), FILE_ALIGN))  # size of raw data
        sec.append(blob)
        if sec[5]:
            raw_offset += sec[5]

    out = bytearray()
    dos = bytearray(0x80)
    dos[0:2] = b"MZ"
    struct.pack_into("<I", dos, 0x3C, 0x80)
    out += dos
    out += b"PE\0\0"
    size_of_code = sum(align_up(s[2], SECTION_ALIGN) for s in pe_sections if s[3] == CHAR_CODE)
    size_of_init = sum(align_up(s[2], SECTION_ALIGN)
                       for s in pe_sections if s[3] in (CHAR_DATA, CHAR_RODATA, CHAR_RELOC))
    size_of_uninit = sum(align_up(s[2], SECTION_ALIGN) for s in pe_sections if s[3] == CHAR_BSS)
    base_of_code = next(s[1] for s in pe_sections if s[0] == ".text")
    out += struct.pack("<HHIIIHH", 0x8664, len(pe_sections), 0, 0, 0, 240, 0x0022)
    optional = struct.pack(
        "<HBBIIIIIQIIHHHHHHIIIIHHQQQQII",
        0x20B,        # PE32+ magic
        1, 0,         # linker version
        size_of_code,
        size_of_init,
        size_of_uninit,
        entry - IMAGE_BASE,
        base_of_code,
        IMAGE_BASE,
        SECTION_ALIGN,
        FILE_ALIGN,
        6, 0,         # OS version
        6, 0,         # image version
        6, 0,         # subsystem version
        0,            # win32 version
        image_size,
        header_size,
        0,            # checksum
        10,           # Subsystem: EFI application
        0,            # DLL characteristics
        0x400000,     # stack reserve
        0x40000,      # stack commit
        0x100000,     # heap reserve
        0x1000,       # heap commit
        0,            # loader flags
        16,           # number of RVA and sizes
    )
    assert len(optional) == 112, len(optional)
    out += optional
    out += b"\0" * 128  # 16 empty data directories: no imports or exceptions
    if reloc_blob:
        # DataDirectory[5] = base relocation table.
        optional_off = 0x80 + 4 + 20
        struct.pack_into("<II", out, optional_off + 112 + 5 * 8, reloc_rva, len(reloc_blob))
    for sec in pe_sections:
        name, rva, virtual_size, characteristics = sec[0], sec[1], sec[2], sec[3]
        raw_size = sec[5]
        raw_pointer = sec[4] if raw_size else 0
        out += struct.pack(
            "<8sIIIIIIHHI",
            name.encode(), virtual_size, rva, raw_size, raw_pointer, 0, 0, 0, 0,
            characteristics)
    assert len(out) <= header_size, (len(out), header_size)
    out += b"\0" * (header_size - len(out))
    for sec in pe_sections:
        if sec[5]:
            out += sec[6].ljust(sec[5], b"\0")
    assert len(out) == raw_offset
    out_path.write_bytes(out)
    print(f"PE32+ {out_path.name}: entry {entry - IMAGE_BASE:#x}, image {image_size:#x}, "
          f"sections {[s[0] for s in pe_sections]}")


if __name__ == "__main__":
    main()
