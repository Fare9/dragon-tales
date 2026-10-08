#!/usr/bin/env python3
"""Generate minimal, valid ELF64 and PE32+ images as a C++ byte-array header.

The loader tests embed their inputs as byte arrays (the same convention as
tests/obfush/TestObfush.cpp), so they need no toolchain or on-disk fixture at
test time. Both images are hand-built here with just enough structure for
llvm::object to parse them the way a real executable would:

  * tiny ELF64 x86-64 ET_EXEC: one PT_LOAD, a .text with `xor eax,eax; ret`,
    an entry point into it, and a .symtab with one STT_FUNC symbol "myfunc".
  * tiny PE32+ x86-64: DOS stub, PE header, PE32+ optional header with an
    ImageBase and entry RVA, and one executable ".text" section.

Re-run to regenerate fixtures.hpp after changing either image.
"""
import struct

TEXT = bytes([0x55, 0x48, 0x89, 0xE5, 0x31, 0xC0, 0x5D, 0xC3])  # push/mov/xor/pop/ret


def align(n, a):
    return (n + a - 1) & ~(a - 1)


def build_elf():
    EM_X86_64 = 62
    base = 0x400000

    ehdr_size, phdr_size, shdr_size, sym_size = 64, 56, 64, 24

    off_phdr = ehdr_size
    off_text = off_phdr + phdr_size
    text_addr = base + off_text

    shstrtab = b"\x00.text\x00.symtab\x00.strtab\x00.shstrtab\x00"
    name_text = shstrtab.index(b".text\x00")
    name_symtab = shstrtab.index(b".symtab\x00")
    name_strtab = shstrtab.index(b".strtab\x00")
    name_shstr = shstrtab.index(b".shstrtab\x00")

    strtab = b"\x00myfunc\x00"
    name_myfunc = strtab.index(b"myfunc\x00")

    off_shstr = off_text + len(TEXT)
    off_strtab = off_shstr + len(shstrtab)
    off_symtab = align(off_strtab + len(strtab), 8)

    # Section index 1 == .text, used as st_shndx.
    symtab = struct.pack("<IBBHQQ", 0, 0, 0, 0, 0, 0)  # null symbol
    st_info = (1 << 4) | 2  # STB_GLOBAL, STT_FUNC
    symtab += struct.pack("<IBBHQQ", name_myfunc, st_info, 0, 1, text_addr, len(TEXT))

    off_shdr = align(off_symtab + len(symtab), 8)
    e_entry = text_addr

    def shdr(name, typ, flags, addr, off, size, link, info, align_, entsize):
        return struct.pack("<IIQQQQIIQQ", name, typ, flags, addr, off, size,
                           link, info, align_, entsize)

    sections = b""
    sections += shdr(0, 0, 0, 0, 0, 0, 0, 0, 0, 0)                      # 0 null
    sections += shdr(name_text, 1, 0x6, text_addr, off_text, len(TEXT),  # 1 .text
                     0, 0, 1, 0)
    sections += shdr(name_symtab, 2, 0, 0, off_symtab, len(symtab),      # 2 .symtab
                     3, 1, 8, sym_size)
    sections += shdr(name_strtab, 3, 0, 0, off_strtab, len(strtab),      # 3 .strtab
                     0, 0, 1, 0)
    sections += shdr(name_shstr, 3, 0, 0, off_shstr, len(shstrtab),      # 4 .shstrtab
                     0, 0, 1, 0)
    e_shnum, e_shstrndx = 5, 4

    ehdr = b"\x7fELF" + bytes([2, 1, 1, 0]) + b"\x00" * 8  # ident: 64-bit LE SysV
    ehdr += struct.pack("<HHIQQQIHHHHHH",
                        2, EM_X86_64, 1, e_entry, off_phdr, off_shdr, 0,
                        ehdr_size, phdr_size, 1, shdr_size, e_shnum, e_shstrndx)

    filesz = off_shdr + len(sections)
    # PT_LOAD mapping the whole file at `base`, R+X.
    phdr = struct.pack("<IIQQQQQQ", 1, 0x5, 0, base, base, filesz, filesz, 0x1000)

    img = bytearray(filesz)
    img[0:len(ehdr)] = ehdr
    img[off_phdr:off_phdr + len(phdr)] = phdr
    img[off_text:off_text + len(TEXT)] = TEXT
    img[off_shstr:off_shstr + len(shstrtab)] = shstrtab
    img[off_strtab:off_strtab + len(strtab)] = strtab
    img[off_symtab:off_symtab + len(symtab)] = symtab
    img[off_shdr:off_shdr + len(sections)] = sections
    return bytes(img), text_addr


def build_pe():
    IMAGE_BASE = 0x140000000
    entry_rva = 0x1000
    sect_align, file_align = 0x1000, 0x200

    e_lfanew = 0x40
    dos = bytearray(e_lfanew)
    dos[0:2] = b"MZ"
    dos[0x3C:0x40] = struct.pack("<I", e_lfanew)

    # COFF file header: Machine=AMD64, 1 section, PE32+ optional header (0xF0).
    coff = struct.pack("<HHIIIHH", 0x8664, 1, 0, 0, 0, 0xF0, 0x0022)

    num_dirs = 16
    headers_end = e_lfanew + 4 + len(coff) + 0xF0 + 40
    size_headers = align(headers_end, file_align)
    raw_ptr = size_headers
    raw_size = align(len(TEXT), file_align)
    size_image = align(entry_rva + max(len(TEXT), sect_align), sect_align)

    # PE32+ optional header (magic 0x20B). ImageBase is 8 bytes; no BaseOfData.
    opt = struct.pack("<HBBIIII", 0x20B, 14, 0, raw_size, 0, 0, entry_rva)
    opt += struct.pack("<IQ", 0, IMAGE_BASE)           # BaseOfCode, ImageBase
    opt += struct.pack("<IIHHHHHH", sect_align, file_align, 6, 0, 0, 0, 6, 0)
    opt += struct.pack("<IIII", 0, size_image, size_headers, 0)  # ...,SizeOfImage,SizeOfHeaders,CheckSum
    opt += struct.pack("<HH", 3, 0)                    # Subsystem=console, DllCharacteristics
    opt += struct.pack("<QQQQ", 0x100000, 0x1000, 0x100000, 0x1000)  # stack/heap
    opt += struct.pack("<II", 0, num_dirs)             # LoaderFlags, NumberOfRvaAndSizes
    opt += b"\x00" * (num_dirs * 8)                    # data directories
    assert len(opt) == 0xF0, len(opt)

    name = b".text".ljust(8, b"\x00")
    sect = name + struct.pack("<IIIIIIHHI", len(TEXT), entry_rva, raw_size,
                              raw_ptr, 0, 0, 0, 0, 0x60000020)

    img = bytearray(raw_ptr + raw_size)
    img[0:len(dos)] = dos
    p = e_lfanew
    img[p:p + 4] = b"PE\x00\x00"; p += 4
    img[p:p + len(coff)] = coff; p += len(coff)
    img[p:p + len(opt)] = opt; p += len(opt)
    img[p:p + len(sect)] = sect
    img[raw_ptr:raw_ptr + len(TEXT)] = TEXT
    return bytes(img), IMAGE_BASE + entry_rva


def emit(name, data):
    out = f"static const unsigned char {name}[] = {{\n"
    for i in range(0, len(data), 12):
        row = ", ".join(f"0x{b:02x}" for b in data[i:i + 12])
        out += f"    {row},\n"
    out += "};\n"
    return out


def main():
    elf, elf_entry = build_elf()
    pe, pe_entry = build_pe()
    with open("fixtures.hpp", "w") as f:
        f.write("// Generated by make_fixtures.py - do not edit by hand.\n")
        f.write("// Minimal ELF64 and PE32+ x86-64 images for the loader tests.\n")
        f.write("#pragma once\n\n")
        f.write(f"// Entry point of the ELF image / address of symbol \"myfunc\".\n")
        f.write(f"static constexpr unsigned long long kTinyElfX64Entry = 0x{elf_entry:x}ULL;\n")
        f.write(f"static constexpr unsigned long long kTinyPeX64Entry  = 0x{pe_entry:x}ULL;\n\n")
        f.write(emit("kTinyElfX64", elf))
        f.write("\n")
        f.write(emit("kTinyPeX64", pe))
    print(f"ELF: {len(elf)} bytes, entry 0x{elf_entry:x}")
    print(f"PE:  {len(pe)} bytes, entry 0x{pe_entry:x}")


if __name__ == "__main__":
    main()
