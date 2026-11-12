#!/usr/bin/env python3
"""Makes a uClinux bFLT (rev 4, FLAT_FLAG_RAM) executable from two links of a program.

usage: mkflat.py build/NAME out.bflt [stack_size]
  build/NAME.0.bin / .0x100000.bin   raw images linked at 0 and 0x100000 (user/build-user.sh)
  build/NAME.0.map                    lit map with __etext, __edata, __end (user/crt0.ast)

Address 0 is the first byte after the 64-byte header (fs/binfmt_flat.c calc_reloc).
Words whose value differs by exactly the base delta between the two links are pointers:
they become relocations, stored big-endian like all header fields; the loader reads them
with ntohl. RAM mode keeps text and data adjacent, which gcc-t800's PC-relative data
addressing needs.
"""
import struct, sys

DELTA = 0x100000
HDR = 64


def symbols(path):
    m = {}
    for line in open(path):
        p = line.split()
        if len(p) == 2:
            try:
                m[p[0]] = int(p[1]) & 0xFFFFFFFF
            except ValueError:
                pass
    return m


def main():
    base, out = sys.argv[1], sys.argv[2]
    stack = int(sys.argv[3], 0) if len(sys.argv) > 3 else 8192
    a = open(base + ".0.bin", "rb").read()
    b = open(base + ".0x100000.bin", "rb").read()
    sym = symbols(base + ".0.map")
    etext, edata, end = sym["__etext"], sym["__edata"], sym["__end"]
    entry = sym["@@hook"]
    assert len(a) == len(b) == end, (len(a), len(b), end)
    image = bytearray(a[:edata])              # text + data; bss is zeroed by the loader
    relocs = []
    for off in range(0, edata - 3, 4):
        va, vb = struct.unpack_from("<I", a, off)[0], struct.unpack_from("<I", b, off)[0]
        if va == vb:
            continue
        assert (vb - va) & 0xFFFFFFFF == DELTA, f"word at {off:#x}: {va:#x} vs {vb:#x}"
        relocs.append(off)
        struct.pack_into(">I", image, off, va)  # pointer value big-endian, as the loader wants
    # any other difference (unaligned) would be a non-PC-relative code reference
    for off in range(edata):
        if a[off] != b[off] and not any(r <= off < r + 4 for r in relocs):
            sys.exit(f"unaligned difference at {off:#x}")
    text_len, data_len = etext, edata - etext
    hdr = b"bFLT" + struct.pack(">IIIIIIIII", 4, HDR + entry, HDR + text_len,
                                HDR + edata, HDR + end, stack, HDR + edata,
                                len(relocs), 0x1) + bytes(24)
    assert len(hdr) == HDR
    data = hdr + image + b"".join(struct.pack(">I", r) for r in relocs)
    open(out, "wb").write(data)
    print(f"{out}: text {text_len} data {data_len} bss {end - edata} relocs {len(relocs)} "
          f"entry {entry:#x}")


if __name__ == "__main__":
    main()
