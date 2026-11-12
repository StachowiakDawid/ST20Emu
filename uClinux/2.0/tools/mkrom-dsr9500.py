#!/usr/bin/env python3
"""Builds a 2 MB DSR9500 flash image that boots the uClinux kernel.

  0x7fe00000  kernel image (linux.bin, linked at 0xc0001000)
  0x7fe80000  root romfs image, read in place by the kernel (asm-st20/board.h ST20_ROMFS_ADDR)
  0x7fff0000  loader: EMI pokes, clock/SDRAM init (call 0x7fff1000), copy kernel to SDRAM,
              jump to it
  0x7fff1000  boot init blob (user/boot/bootinit.c: Samsung's dsr_ckg_init + dsr_sdram_init)
  0x7fffffe0  long jump to the loader (ldc LOADER; gcall)
  0x7ffffffe  reset entry: 2-byte j to 0x7fffffe0

The pokes are the DSR9500 boot poke table (Decompilation/DSR9500 boot-tables.md:
Samsung's dsrConfigureEMI_SDRAM twice around the MinInt fill of 0x80000000-0x8000003c),
applied with devsw like ST's romload, before any workspace exists.

The init blob runs with Wptr in on-chip SRAM (0x80000700) like the original romload's
calls; the pokes need no workspace.

usage: mkrom-dsr9500.py linux.bin romfs.img bootinit.bin out.bin
"""
import sys

FLASH_BASE, FLASH_SIZE = 0x7FE00000, 0x200000
KERNEL_ROM, KERNEL_RAM = 0x7FE00000, 0xC0001000
ROMFS = 0x7FE80000
LOADER = 0x7FFF0000
BOOTINIT = 0x7FFF1000
BOOT_WPTR = 0x80000700
LONGJUMP = 0x7FFFFFE0
ENTRY = 0x7FFFFFFE

EMI = [(0x2070, 0x0C00), (0x2000, 0x132A), (0x2004, 0xFFF8), (0x2008, 0x012A),
       (0x200C, 0x095A), (0x2010, 0x3679), (0x2014, 0xF90F), (0x2018, 0xF90F),
       (0x201C, 0x0000), (0x2058, 0x0022), (0x205C, 0x0022), (0x2020, 0x16C9),
       (0x2024, 0x8A00), (0x2028, 0x8A00), (0x202C, 0x0002), (0x2030, 0x16D1),
       (0x2014, 0x8900), (0x2018, 0x8900), (0x201C, 0x0000), (0x2060, 0x0001)]
POKES = EMI + [(0x80000000 + 4 * i, 0x80000000) for i in range(16)] + EMI
assert len(POKES) == 56


def direct(fn, v):
    """prefix-encode direct function fn (0-15) with operand v"""
    v &= 0xFFFFFFFF
    v = v - (1 << 32) if v & 0x80000000 else v
    if 0 <= v < 16:
        return bytes([fn << 4 | v])
    if v >= 16:
        return direct(2, v >> 4) + bytes([fn << 4 | (v & 15)])
    return direct(6, (~v) >> 4) + bytes([fn << 4 | (v & 15)])


def ldc(v):
    return direct(4, v & 0xFFFFFFFF)


def opr(code):
    return direct(15, code)


GCALL, MOVE, DEVSW, GAJW = opr(0x06), opr(0x4A), opr(0xF5), opr(0x3C)


def main():
    kernel = open(sys.argv[1], "rb").read()
    romfs = open(sys.argv[2], "rb").read()
    bootinit = open(sys.argv[3], "rb").read()
    assert KERNEL_ROM + len(kernel) <= ROMFS, "kernel overlaps romfs"
    assert ROMFS + len(romfs) <= LOADER, "romfs too big for flash"
    rom = bytearray(b"\xff" * FLASH_SIZE)
    rom[0:len(kernel)] = kernel
    rom[ROMFS - FLASH_BASE:ROMFS - FLASH_BASE + len(romfs)] = romfs

    code = bytearray()
    for addr, value in POKES:
        code += ldc(value) + ldc(addr) + DEVSW          # B = value, A = address
    code += ldc(BOOT_WPTR) + GAJW
    # call BOOTINIT: the offset is relative to the next instruction, and its prefix length
    # depends on the offset itself
    here = LOADER + len(code)
    size = 1
    while True:
        call = direct(9, BOOTINIT - (here + size))
        if len(call) == size:
            break
        size = len(call)
    code += call
    code += ldc(KERNEL_ROM) + ldc(KERNEL_RAM) + ldc(len(kernel)) + MOVE  # C src, B dst, A len
    code += ldc(KERNEL_RAM) + GCALL                     # head.ast sets up Wptr itself
    o = LOADER - FLASH_BASE
    assert LOADER + len(code) <= BOOTINIT, "loader overlaps the boot init blob"
    rom[o:o + len(code)] = code
    o = BOOTINIT - FLASH_BASE
    assert BOOTINIT + len(bootinit) <= LONGJUMP
    rom[o:o + len(bootinit)] = bootinit

    jl = ldc(LOADER) + GCALL
    o = LONGJUMP - FLASH_BASE
    rom[o:o + len(jl)] = jl
    assert LONGJUMP + len(jl) <= ENTRY

    # j offset is relative to the next instruction, 0x80000000; must encode in 2 bytes
    j = direct(0, LONGJUMP - (ENTRY + 2))
    assert len(j) == 2, j
    rom[ENTRY - FLASH_BASE:] = j

    open(sys.argv[4], "wb").write(rom)
    print(f"kernel {len(kernel)} bytes, romfs {len(romfs)} bytes, loader {len(code)} bytes at {LOADER:#x}, entry {j.hex()}")


if __name__ == "__main__":
    main()
