#!/bin/bash
# Regenerates patches/ from the working tree in src/ (run on the host, from uClinux/2.4/).
# src/ is not in git; patches/ is, so run this before committing port changes.
#   patches/linux-2.4.x-st20.diff   uClinux-dist linux-2.4.x (incl. new arch/st20,
#                                   include/asm-st20, MTD map, frame buffer driver,
#                                   include/linux/dvb DVB API v3 headers)
#   patches/linux-2.4.x.config      kernel .config
#   patches/uClibc-0.9.26-st20.diff uClibc 0.9.26 (src/uClibc-0.9.26 vs dl/ tarball; new
#                                   libc/sysdeps/linux/st20, extra/Configs/Config.st20)
#   patches/uClibc-0.9.26.config    uClibc .config
#   patches/busybox-1.00-st20.diff  busybox 1.00 (src/busybox-1.00 vs dl/ tarball); its
#                                   config is user/busybox.config
# The netfilter files differing only in case (ipt_DSCP.h / ipt_dscp.h ...) show up as
# modified on a case-insensitive file system and are left out.
set -e
cd "$(dirname "$0")/.."
mkdir -p patches
k=src/uclinux-dist
git -C $k add -N linux-2.4.x/arch/st20 linux-2.4.x/include/asm-st20 \
    linux-2.4.x/drivers/mtd/maps/dsr9500-flash.c linux-2.4.x/drivers/video/st20fb.c \
    linux-2.4.x/include/linux/dvb
git -C $k diff -- linux-2.4.x \
    ':(exclude)*.o' ':(exclude)*.a' ':(exclude)*/.depend' ':(exclude)*.flags' \
    ':(exclude)*netfilter*' \
    ':(exclude)linux-2.4.x/include/linux/autoconf.h' ':(exclude)linux-2.4.x/include/asm' \
    > patches/linux-2.4.x-st20.diff
cp $k/linux-2.4.x/.config patches/linux-2.4.x.config
# user space: diff against freshly unpacked tarballs, sources only
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
tar xjf dl/uClibc-0.9.26.tar.bz2 -C $t
tar xjf dl/busybox-1.00.tar.bz2 -C $t
udiff() {  # NAME: diff -urN of the source files of $t/NAME and src/NAME
  (cd src && diff -urN -x '*.o' -x '*.a' -x '.config*' -x '.depend' -x '*.cmd' \
    -x include -x lib -x conf -x mconf -x '*.so' -x 'lex.zconf.c' -x 'zconf.tab.*' \
    -x 'lkc_defs.h' -x busybox -x busybox.links -x 'Makefile' -x .hdepend -x interp.c \
    "$t/$1" "$1" | sed "s|$t/||g") || true
}
# uClibc: include/ is partly generated (bits/, asm, linux links), so its tracked headers are
# diffed one by one; Makefiles are diffed separately (busybox copies them into the tree)
{
  udiff uClibc-0.9.26
  for f in include/features.h include/alloca.h include/string.h \
           libc/unistd/Makefile; do
    diff -uN "$t/uClibc-0.9.26/$f" "src/uClibc-0.9.26/$f" \
      --label "uClibc-0.9.26/$f" --label "uClibc-0.9.26/$f" || true
  done
  diff -uN /dev/null src/uClibc-0.9.26/libc/sysdeps/linux/st20/Makefile \
    --label /dev/null --label uClibc-0.9.26/libc/sysdeps/linux/st20/Makefile || true
} > patches/uClibc-0.9.26-st20.diff
cp src/uClibc-0.9.26/.config patches/uClibc-0.9.26.config
udiff busybox-1.00 > patches/busybox-1.00-st20.diff
wc -l patches/*.diff
