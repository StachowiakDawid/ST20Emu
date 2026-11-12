#!/bin/bash
# Builds the kernel test programs (fbtest, evtest) against the 2.0 port's uC-libc, in the
# st20-build container: ./dbuild.sh /work/port-tools/test/build.sh (dbuild.sh mounts
# ../2.0/tools over /work/tools, and this port's tools/ at /work/port-tools). Output:
# build/test/NAME.0.bin, .0x100000.bin, .0.map; mkimage.sh makes the bFLTs with
# ../2.0/tools/mkflat.py.
set -e
export PATH=/work/tools/bin:$PATH
LIBGCC=$(t450-gcc -print-libgcc-file-name)
LIBC=/work/2.0/src/uclinux-dist/lib/libc
O=/work/build/test
mkdir -p $O
for p in fbtest evtest; do
  t450-gcc -O2 -mt450 -Wall -D__restrict= -I$LIBC/include -I/work/2.0/user/include -DEMBED \
    -c /work/port-tools/test/$p.c -o $O/$p.o
  for base in 0 0x100000; do
    st20-ldr -T $base -m$O/$p.$base.map $LIBC/crt0.o $O/$p.o $LIBC/libc.a $LIBGCC -o $O/$p.$base.lit
    crossload -o $O/$p.$base.bin $O/$p.$base.lit
  done
done
echo TESTS OK
