#!/bin/bash
# Builds the 2.4 port's user space: uClibc 0.9.26 + busybox 1.00 (src/uClibc-0.9.26,
# src/busybox-1.00, from dl/ by tools/setup-src.sh), as bFLT files in user/build/.
# Run on the host from uClinux/2.4 (or anywhere): the compile/link part runs in the
# st20-build container (../dbuild.sh), then tools/mkflat.py makes the bFLTs on the host.
#   user/build-user.sh            everything (uClibc only if lib/libc.a is missing)
#   user/build-user.sh uclibc     rebuild uClibc (make; edits are picked up), then the rest
#   user/build-user.sh busybox    only busybox (and the bFLTs)
# Output: user/build/*.bflt, user/build/busybox.links (applet paths, one per line).
set -e
H=$(cd "$(dirname "$0")/.." && pwd)
if [ ! -d /work/tools/bin ]; then
  "$H/dbuild.sh" /work/user/build-user.sh "$@"
  cd "$H/user/build"
  for p in $(cat programs); do
    stack=$(sed -n "s/^$p //p" stacks); stack=${stack:-8192}
    python3 "$H/../2.0/tools/mkflat.py" $p $p.bflt $stack
  done
  echo "bFLTs: $(cat programs | tr '\n' ' ')"
  exit 0
fi

# ---- in the container ----
export PATH=/work/tools/bin:$PATH:/work/user/bin
UC=/work/src/uClibc-0.9.26
BB=/work/src/busybox-1.00
# t450-gcc with cpp 4.3 (user/bin/cpp, see there); never -O0
CC="t450-gcc -B/work/user/bin/"
GCCLIB=$(t450-gcc -print-libgcc-file-name)
GCCINC=$(dirname $GCCLIB)/include
# -fno-builtin: from when gcc-t800's __builtin_alloca was broken (logs/gcc-2.7.2-t800-alloca*.c,
# fixed 2026-11-12); kept (no other builtins have been tested); <alloca.h> uses __builtin_alloca
UCFLAGS="-O2 -fno-builtin -mt450 -Wall -nostdinc -I$UC/include -I$GCCINC"
mkdir -p /work/user/build
cd /work/user/build
: > programs; : > stacks
# libgcc without its exit() (_exit.o, which needs _cleanup): lit loads every archive member
# that defines a needed symbol, even one already defined, so it would come in next to
# uClibc's exit
LIBGCC=/work/user/build/libgcc.a
cp $GCCLIB $LIBGCC && ar d $LIBGCC _exit.o

build_uclibc() {
  cd $UC
  make CROSS=t450- "CC=$CC" headers >/dev/null
  # not -j: every directory adds its objects to libc.a with ar (parallel ars lose members)
  make CROSS=t450- "CC=$CC" subdirs > /work/user/build/uclibc.log 2>&1 ||
    { tail -30 /work/user/build/uclibc.log; exit 1; }
  cd /work/user/build
}
if [ "$1" = uclibc ] || [ ! -f $UC/lib/libc.a ]; then build_uclibc; fi

# link NAME [STACK] OBJECTS...: two links (bases 0, 0x100000) + raw images for mkflat.py
link() {
  local name=$1 stack=$2; shift 2
  # lit directly: every archive here is flat with unique member names (st20-ldr would
  # copy all ~700 libc members per link)
  for base in 0 0x100000; do
    lit -T $base -m$name.$base.map $UC/lib/crt0.o "$@" $UC/lib/libm.a $UC/lib/libc.a \
      $LIBGCC -o $name.$base.lit
    crossload -o $name.$base.bin $name.$base.lit
  done
  echo $name >> programs
  echo "$name $stack" >> stacks
}

# busybox 1.00: config user/busybox.config (made from user/busybox-seed.config with
# `conf -o`, the seed lists the wanted options); objects by its Makefile, link here
build_busybox() {
  cd $BB
  # a changed configuration rebuilds everything (objects do not depend on it reliably);
  # conf writes include/config.h, with BB_VER/BB_BT from the environment (as the Makefile)
  if ! cmp -s /work/user/busybox.config .config || [ ! -f include/config.h ]; then
    cp /work/user/busybox.config .config
    find . -name '*.o' -not -path './scripts/*' -exec rm -f {} +
    VERSION=1.00 BUILDTIME=$(TZ=UTC date -u "+%Y.%m.%d-%H:%M%z") \
      sh -c 'yes "" | head -3000 | ./scripts/config/conf -o sysdeps/linux/Config.in' >/dev/null
    cp .config /work/user/busybox.config
  fi
  # archives are made with ar r: drop them so members of applets switched off go away
  find . -name '*.a' -not -path './scripts/*' -exec rm -f {} +
  make -j6 CROSS=t450- "CC=$CC" "OPTIMIZATION=-O2 -fno-builtin" \
    "CROSS_CFLAGS=-mt450 -nostdinc -I/work/user/include -I$UC/include -I$GCCINC -D__uClinux__" \
    WARNINGS=-Wall -f Makefile -f /work/user/busybox-libs.mk bb-libs \
    > /work/user/build/busybox.log 2>&1 || { grep -a -B5 'rror' /work/user/build/busybox.log | tail -40; exit 1; }
  cd /work/user/build
  # archives without duplicate members (libunarchive.a gets some objects up to three times;
  # lit would load every copy: "COMMON block has tag 16")
  rm -rf bblibs; mkdir -p bblibs
  for l in $(cat busybox.libs); do
    n=$(echo ${l#$BB/} | tr / _); mkdir bblibs/$n.d
    (cd bblibs/$n.d && ar x $l && if ls *.o >/dev/null 2>&1; then ar rcS ../$n *.o; fi)
    rm -rf bblibs/$n.d
  done
  link busybox 32768 bblibs/*.a
  # applet paths (as busybox.mkll writes them), without /bin/busybox itself
  grep -v '^/bin/busybox$' $BB/busybox.links > busybox.links
}
[ -f $BB/scripts/config/conf ] || (cd $BB && make scripts/config/conf >/dev/null)
build_busybox
[ "$1" = busybox ] && { echo USER OK; exit 0; }

# test programs: user/test (uClibc tests), tools/test (kernel fb/input tests, mounted at
# /work/port-tools by dbuild.sh), sigtest/stattest from ../2.0/user (signals, struct stat),
# user/net (nettest: loopback/SLIP tests; modtool: insmod/rmmod/lsmod for .mod modules)
for t in /work/user/test/uctest.c /work/user/test/retest.c /work/port-tools/test/fbtest.c /work/port-tools/test/evtest.c \
         /work/2.0/user/sigtest.c /work/2.0/user/stattest.c /work/user/net/nettest.c /work/user/net/modtool.c; do
  n=$(basename $t .c)
  $CC $UCFLAGS -c $t -o $n.o
  link $n 16384 $n.o
done
echo USER OK
