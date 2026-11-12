#!/usr/bin/env python3
"""mkmod.py: ST20 kernel module image (.mod) from modlink.sh's two links.

  mkmod.py DIR NAME KERNEL_BIN KERNEL_MAP OUT.mod

modlink.sh links the module at A = 0x50000000 and B = 0x51000000: about 2 GB from the
kernel at 0xc0001000, so lit gives every reference to a kernel symbol the full 8-byte
pfix/nfix sequence in both links, and any load address in RAM fits that width.
The images differ only where code or data depends on the load address:

  * a data or text word that is B - A larger in B: an absolute pointer into the module
    (initialiser such as a file_operations entry) -> ABS reloc, loader adds (load - A);
  * a pfix/nfix sequence whose operand is B - A smaller in B: a PC-relative reference from
    the module to the kernel (call, ldc sym-L; ldpi) -> PCREL reloc: operand at the load
    address = operand_A - (load - A), re-encoded by the loader in the same number of bytes
    (leading pfix 0 pad a shorter encoding);
  * a sequence whose operand is B - A larger: an absolute module address in code -> PCREL
    reloc with the opposite sign (flag bit).

Module-internal references are PC-relative and kernel references in data are absolute, so
neither changes.  The kernel checks the image against itself with a hash of linux_banner
(includes the build number and time of the exact kernel the module was linked against).

Format (little endian u32): magic 'S20M', version 1, banner hash, link base A, image size
(text + data), bss size, init offset, exit offset (~0 = none), n_abs, n_pcrel,
name[24]; image; n_abs offsets; n_pcrel pairs (len << 24 | offset | neg << 23, operand_A).
"""
import struct
import sys

A, B = 0x50000000, 0x51000000
DELTA = B - A
MAGIC = 0x4d303253  # "S20M"
PFIX, NFIX = 2, 6


def readmap(path):
    m = {}
    for line in open(path):
        p = line.split()
        if len(p) == 2:
            try:
                m[p[0]] = int(p[1]) & 0xffffffff
            except ValueError:
                m[p[0]] = p[1]
    return m


def s32(v):
    v &= 0xffffffff
    return v - (1 << 32) if v & 0x80000000 else v


def decode(b):
    o = 0
    for x in b[:-1]:
        f, d = x >> 4, x & 15
        if f == PFIX:
            o = ((o | d) << 4) & 0xffffffff
        elif f == NFIX:
            o = ((~(o | d)) << 4) & 0xffffffff
        else:
            raise ValueError('not a prefix')
    return b[-1] >> 4, s32(o | (b[-1] & 15))


def encode(f, v):
    if 0 <= v < 16:
        return [f << 4 | v]
    if v >= 16:
        return encode(PFIX, v >> 4) + [f << 4 | (v & 15)]
    return encode(NFIX, (~v) >> 4) + [f << 4 | (v & 15)]


def fnv(data):
    h = 0x811c9dc5
    for c in data:
        h = ((h ^ c) * 0x01000193) & 0xffffffff
    return h


def isprefix(x):
    return x >> 4 in (PFIX, NFIX)


def main():
    d, name, kbin, kmap, out = sys.argv[1:6]
    ma, mb = readmap(f'{d}/{name}.{A:#x}.map'), readmap(f'{d}/{name}.{B:#x}.map')
    size = ma['text@@size'] + ma['data@@size']
    tsize = ma['text@@size']
    bss = ma['bss@@size']
    assert (mb['text@@size'], mb['data@@size'], mb['bss@@size']) == (ma['text@@size'], ma['data@@size'], bss), \
        'segment sizes differ between the two links'
    ia = open(f'{d}/{name}.{A:#x}.bin', 'rb').read()[:size]
    ib = open(f'{d}/{name}.{B:#x}.bin', 'rb').read()[:size]
    assert len(ia) == len(ib) == size
    initvar, *rest = open(f'{d}/{name}.entry').read().split()
    exitvar = rest[0] if rest else None

    absr, pcr = [], []
    i = 0
    while i < size:
        if ia[i] == ib[i]:
            i += 1
            continue
        done = False
        if i < tsize and (isprefix(ia[i]) or (i > 0 and isprefix(ia[i - 1]))):
            s = i
            while s > 0 and isprefix(ia[s - 1]) and isprefix(ib[s - 1]):
                s -= 1
            e = i
            while e < tsize and isprefix(ia[e]):
                e += 1
            if e < tsize and all(isprefix(x) for x in ib[s:e]) and not isprefix(ib[e]):
                fa, va = decode(ia[s:e + 1])
                fb, vb = decode(ib[s:e + 1])
                if fa == fb and vb - va in (-DELTA, DELTA):
                    pcr.append((s, e + 1 - s, vb - va == DELTA, va))
                    i = e + 1
                    done = True
        if not done:
            w = i & ~3
            wa, = struct.unpack_from('<I', ia, w)
            wb, = struct.unpack_from('<I', ib, w)
            if (wb - wa) & 0xffffffff == DELTA and A <= wa < A + size + bss + 1:
                absr.append(w)
                i = w + 4
                done = True
        if not done:
            raise SystemExit(f'mkmod: cannot classify difference at offset {i:#x} '
                             f'({"text" if i < tsize else "data"}): {ia[i]:02x} vs {ib[i]:02x}')

    def entry(var):
        if not var:
            return 0xffffffff
        off = ma[var] - A
        fn, = struct.unpack_from('<I', ia, off)
        assert A <= fn < A + tsize, f'{var} does not point into the module text'
        return fn - A

    km = readmap(kmap)
    kb = open(kbin, 'rb').read()
    # const char *linux_banner (init/version.c): the pointer, then the string
    ptr, = struct.unpack_from('<I', kb, km['_linux_banner'] - km['text@@start'])
    bo = ptr - km['text@@start']
    banner = kb[bo:kb.index(b'\0', bo)]
    hdr = struct.pack('<10I24s', MAGIC, 1, fnv(banner), A, size, bss, entry(initvar), entry(exitvar),
                      len(absr), len(pcr), name.encode()[:23])
    body = bytearray(ia)
    rel = b''.join(struct.pack('<I', o) for o in absr)
    for (o, n, neg, va) in pcr:
        assert n < 128 and o < (1 << 23)
        rel += struct.pack('<Ii', n << 24 | (1 << 23 if neg else 0) | o, va)
    open(out, 'wb').write(hdr + bytes(body) + rel)
    print(f'{out}: {name}, {tsize} text + {size - tsize} data + {bss} bss bytes, '
          f'{len(absr)} abs + {len(pcr)} pc-relative relocations, kernel "{banner.decode(errors="replace").strip()[:60]}"')


if __name__ == '__main__':
    main()
