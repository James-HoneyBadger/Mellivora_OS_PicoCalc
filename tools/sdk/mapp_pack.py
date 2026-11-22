#!/usr/bin/env python3
"""Pack a linked ARM ELF into the relocatable Mellivora .mapp container."""

import argparse
import struct
import sys
from pathlib import Path

ELF_HEADER = struct.Struct("<16sHHIIIIIHHHHHH")
SECTION_HEADER = struct.Struct("<IIIIIIIIII")
SYMBOL = struct.Struct("<IIIBBH")
REL = struct.Struct("<II")
MAPP_HEADER = struct.Struct("<4sHHIIIII32s")
SHT_SYMTAB = 2
SHT_REL = 9
SHT_RELA = 4
SHT_NOBITS = 8
SHF_ALLOC = 2
R_ARM_ABS32 = 2
R_ARM_TARGET1 = 38


def fail(message):
    raise ValueError(message)


def c_string(table, offset):
    if offset >= len(table):
        fail("ELF string offset is out of range")
    end = table.find(b"\0", offset)
    if end < 0:
        fail("unterminated ELF string")
    return table[offset:end].decode("ascii", "replace")


def pack_elf(elf_path, app_name):
    data = Path(elf_path).read_bytes()
    if len(data) < ELF_HEADER.size:
        fail("input is not a complete ELF file")
    header = ELF_HEADER.unpack_from(data)
    ident = header[0]
    if ident[:4] != b"\x7fELF" or ident[4] != 1 or ident[5] != 1:
        fail("expected a 32-bit little-endian ELF")
    if header[2] != 40:
        fail("expected an ARM ELF")

    shoff, shentsize, shnum, shstrndx = header[6], header[11], header[12], header[13]
    if shentsize != SECTION_HEADER.size or shnum == 0 or shstrndx >= shnum:
        fail("unsupported ELF section table")
    if shoff + shentsize * shnum > len(data):
        fail("ELF section table is out of range")
    sections = [
        SECTION_HEADER.unpack_from(data, shoff + i * shentsize)
        for i in range(shnum)
    ]
    shstr = sections[shstrndx]
    names = data[shstr[4]:shstr[4] + shstr[5]]
    section_names = [c_string(names, section[0]) for section in sections]
    by_name = {name: index for index, name in enumerate(section_names)}

    if ".app_image" not in by_name or ".app_bss" not in by_name:
        fail("link with tools/sdk/app.ld to produce .app_image and .app_bss")
    image_index = by_name[".app_image"]
    bss_index = by_name[".app_bss"]
    image_section = sections[image_index]
    bss_section = sections[bss_index]
    image_address, image_offset, image_size = image_section[3], image_section[4], image_section[5]
    bss_address, bss_size = bss_section[3], bss_section[5]
    if image_address != 0 or bss_address != image_size:
        fail("application image/BSS must be contiguous and linked at address zero")
    if image_offset + image_size > len(data) or image_size == 0:
        fail("invalid .app_image section")
    image = bytearray(data[image_offset:image_offset + image_size])

    symtab_index = next(
        (i for i, section in enumerate(sections) if section[1] == SHT_SYMTAB),
        None,
    )
    if symtab_index is None:
        fail("ELF has no symbol table")
    symtab = sections[symtab_index]
    if symtab[6] >= shnum or symtab[9] != SYMBOL.size:
        fail("unsupported ELF symbol table")
    strings_section = sections[symtab[6]]
    strings = data[strings_section[4]:strings_section[4] + strings_section[5]]
    symbols = []
    for offset in range(symtab[4], symtab[4] + symtab[5], symtab[9]):
        if offset + SYMBOL.size > len(data):
            fail("ELF symbol table is out of range")
        symbols.append(SYMBOL.unpack_from(data, offset))

    entry_symbol = next(
        (symbol for symbol in symbols
         if c_string(strings, symbol[0]) == "mapp_main"),
        None,
    )
    if entry_symbol is None or entry_symbol[5] != image_index:
        fail("ELF must define mapp_main in .app_image")
    entry_offset = entry_symbol[1] & ~1
    if entry_offset >= image_size or entry_offset & 1:
        fail("mapp_main is outside .app_image or not Thumb aligned")

    relocations = []
    for section_index, section in enumerate(sections):
        if section[1] not in (SHT_REL, SHT_RELA) or section[7] != image_index:
            continue
        if section[1] == SHT_RELA:
            fail("RELA relocations are not supported; use the ARM GNU toolchain")
        if section[6] != symtab_index or section[9] != REL.size:
            fail("unsupported relocation table")
        for offset in range(section[4], section[4] + section[5], section[9]):
            if offset + REL.size > len(data):
                fail("ELF relocation table is out of range")
            relocation_offset, info = REL.unpack_from(data, offset)
            relocation_type = info & 0xFF
            if relocation_type in (R_ARM_ABS32, R_ARM_TARGET1):
                if relocation_offset + 4 > image_size or relocation_offset & 3:
                    fail("absolute relocation is outside or misaligned in .app_image")
                relocations.append(relocation_offset)
            elif relocation_type not in (0, 3, 10, 28, 29, 30, 42):
                fail(f"unsupported ARM relocation type {relocation_type}")

    for index, section in enumerate(sections):
        if index in (image_index, bss_index) or not (section[2] & SHF_ALLOC):
            continue
        if section[5] != 0:
            fail(f"unexpected allocated ELF section: {section_names[index]}")

    name = app_name.encode("utf-8")
    if not name or len(name) > 31:
        fail("application name must be 1-31 UTF-8 bytes")
    packed_header = MAPP_HEADER.pack(
        b"MAPP", 1, 1, MAPP_HEADER.size, image_size, bss_size,
        entry_offset, len(relocations), name.ljust(32, b"\0"),
    )
    relocation_table = b"".join(struct.pack("<I", offset) for offset in relocations)
    return packed_header + relocation_table + image


def pack_bytecode(bytecode_path, app_name):
    image = Path(bytecode_path).read_bytes()
    if not image:
        fail("bytecode image must not be empty")
    name = app_name.encode("utf-8")
    if not name or len(name) > 31:
        fail("application name must be 1-31 UTF-8 bytes")
    return MAPP_HEADER.pack(
        b"MAPP", 1, 2, MAPP_HEADER.size, len(image), 0, 0, 0,
        name.ljust(32, b"\0"),
    ) + image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True, help="launcher title (maximum 31 UTF-8 bytes)")
    parser.add_argument("--bytecode", action="store_true", help="pack an MAPP bytecode image instead of an ARM ELF")
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        result = pack_bytecode(args.input, args.name) if args.bytecode else pack_elf(args.input, args.name)
    except (OSError, ValueError, struct.error) as error:
        print(f"mapp_pack: {error}", file=sys.stderr)
        return 1
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
