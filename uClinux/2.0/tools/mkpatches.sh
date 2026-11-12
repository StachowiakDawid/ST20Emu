#!/bin/bash
# Regenerates patches/ from the working trees in src/ (run on the host, from ucLinux/).
# src/ is not in git; patches/ is, so run this before committing port changes.
#   patches/gcc-2.7.2-st20.diff     gcc-2.7.2 + t800 patch v12 -> src/gcc-2.7.2
#   patches/ttools-2.0beta2.diff    ttools-2.0beta2            -> src/ttools-2.0beta2
#   patches/linux-2.0.x-st20.diff   uClinux-dist linux-2.0.x, lib/libc (uC-libc), user/sash,
#                                   user/busybox (incl. new arch/st20, include/asm-st20, libc
#                                   st20 dirs, libc regex, busybox hexdump.c)
#   patches/linux-2.0.x.config      kernel .config
set -e
cd "$(dirname "$0")/.."
top=$PWD
mkdir -p patches
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT

# gcc: pristine = tarball + t800 patch
(cd "$tmp" && tar xzf "$top/dl/gcc-2.7.2.tar.gz" &&
 cd gcc-2.7.2 && gzip -dc "$top/dl/gcc-2.7.2-t800.12.dif.gz" | patch -p1 -s)
(cd "$tmp" && diff -ru gcc-2.7.2 "$top/src/gcc-2.7.2" |
 sed "s|$top/src/||" | grep -v '^Only in' > "$top/patches/gcc-2.7.2-st20.diff") || true

# ttools: pristine = tarball; only files that exist upstream (build outputs are new files)
(cd "$tmp" && tar xzf "$top/dl/ttools-2.0beta2.tar.gz")
(cd "$tmp" && diff -ru -x '*.o' ttools-2.0beta2 "$top/src/ttools-2.0beta2" |
 sed "s|$top/src/||" | grep -v '^Only in\|^Binary files' > "$top/patches/ttools-2.0beta2.diff") || true

# kernel: against the uClinux-dist checkout (src/uclinux-dist is a sparse git clone)
k=src/uclinux-dist
git -C $k add -N linux-2.0.x/arch/st20 linux-2.0.x/include/asm-st20 \
    lib/libc/st20 lib/libc/include/st20 lib/libc/sysdeps/st20 \
    lib/libc/regex/regex.c lib/libc/regex/Makefile lib/libc/include/regex.h \
    user/busybox/hexdump.c
git -C $k diff -- linux-2.0.x lib/libc user/sash user/busybox \
    ':(exclude)*.o' ':(exclude)*.a' ':(exclude)*/.depend' \
    ':(exclude)linux-2.0.x/include/linux/autoconf.h' ':(exclude)linux-2.0.x/include/asm' \
    ':(exclude)lib/libc/include/asm' ':(exclude)lib/libc/include/linux' \
    ':(exclude)lib/libc/include/machine' ':(exclude)lib/libc/stdio2/stdio.h' \
    > patches/linux-2.0.x-st20.diff
cp $k/linux-2.0.x/.config patches/linux-2.0.x.config

wc -l patches/*.diff
