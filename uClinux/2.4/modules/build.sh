#!/bin/bash
# Builds ST20 kernel modules (in the st20-build container, from uClinux/2.4):
#   ./dbuild.sh /work/modules/build.sh NAME...
# Each modules/NAME.c is compiled like a kernel object of src/uclinux-dist/linux-2.4.x
# (same flags, KBUILD_BASENAME=NAME) and linked against that kernel's linux.map by
# modlink.sh; the host step (mkmod.py, run by mkimage.sh) makes build/mod/NAME.mod.
# Rebuild after every kernel build (the module carries a hash of the kernel's banner).
set -e
export PATH=/work/tools/bin:$PATH
K=/work/src/uclinux-dist/linux-2.4.x
O=/work/build/mod
mkdir -p $O
for m in "$@"; do
  t450-gcc -D__KERNEL__ -I$K/include -Wall -Wstrict-prototypes -Wno-trigraphs -O2 \
    -fomit-frame-pointer -mt450 -DNO_MM -DNO_FPU -fsigned-char \
    -DNO_FORGET -D__linux__ -DUTS_SYSNAME='"uClinux"' -nostdinc \
    -iwithprefix include -DKBUILD_BASENAME=$m -c -o $O/$m.o /work/modules/$m.c
  /work/modules/modlink.sh $O $m $K/linux.map $O/$m.o
done
echo MODULES OK
