#!/usr/bin/env python3
"""addr2sym.py linux.map ADDR...: nearest symbol at or below each address (lit -m map)."""
import sys
syms = []
for line in open(sys.argv[1]):
    p = line.split()
    if len(p) == 2:
        try:
            syms.append(((int(p[1]) & 0xffffffff), p[0]))
        except ValueError:
            pass
syms.sort()
for a in sys.argv[2:]:
    x = int(a, 16)
    best = None
    for v, n in syms:
        if v <= x:
            best = (v, n)
        else:
            break
    print(f"{x:08x} {best[1]}+{x - best[0]:#x}" if best else f"{x:08x} ?")
