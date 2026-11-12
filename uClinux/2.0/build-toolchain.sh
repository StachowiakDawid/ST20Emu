#!/bin/sh
# Builds ttools + gcc-2.7.2-t800 (target t450) into /work/tools.
# Prepare src/ first with tools/setup-src.sh (host), then run inside the st20-build container:
#   docker run --rm --platform linux/386 -v $PWD:/work st20-build sh /work/build-toolchain.sh
set -e
P=/work/tools
T=$P/t450/bin
mkdir -p $P/bin $T $P/t450/lib

# 1. ttools (assembler ast, linker lit, ...). -fwrapv is required: ttools' overflow checks
#    rely on signed wrap-around (see README).
CCX='gcc -fwrapv -fno-strict-aliasing -include errno.h -include string.h -include stdlib.h'
if [ ! -x $P/bin/ast ]; then
  cd /work/src/ttools-2.0beta2
  for d in ast lit litdump dast; do
    (cd $d && rm -f *.o && make -s CC="$CCX" LIBDIR='-DLIBDIR=/work/tools/t450/lib') \
      > /work/src/ttools-$d.log 2>&1 || { tail -30 /work/src/ttools-$d.log; exit 1; }
    cp $d/$d $P/bin/
  done
  # crossload: LIT executable -> raw memory image
  (cd startups/absolute && $CCX -O2 -w -I../shared -I../../shared -o $P/bin/crossload \
      crossload.c ../shared/litload.c)
  echo TTOOLS OK
fi
ln -sf $P/bin/ast $T/as; ln -sf $P/bin/lit $T/ld; ln -sf /usr/bin/ar $T/ar; ln -sf /usr/bin/ranlib $T/ranlib

# 2. gcc-2.7.2 + t800 patches, target t450 (ST20 C2 is closest to T450)
if [ ! -x $P/bin/t450-gcc ]; then
  mkdir -p /work/src/gcc-build && cd /work/src/gcc-build
  [ -f Makefile ] || ../gcc-2.7.2/configure --host=i486-linux --build=i486-linux --prefix=$P --target=t450 > /work/src/gcc-config.log 2>&1 || { tail -30 /work/src/gcc-config.log; exit 1; }
  make xgcc cc1 cpp libgcc.a specs LANGUAGES=c CC='gcc -fwrapv -fno-strict-aliasing' CFLAGS='-O1 -w' LIBGCC1_TEST= > /work/src/gcc-make.log 2>&1 || { grep -iE 'error|Error' /work/src/gcc-make.log | head -20; tail -15 /work/src/gcc-make.log; exit 1; }
  L=$P/lib/gcc-lib/t450/2.7.2-t800.12; mkdir -p $L/include
  cp cc1 cpp libgcc.a specs $L/; cp -r include/* $L/include/; cp xgcc $P/bin/t450-gcc
  echo GCC OK
fi

# 3. kernel build names (CROSS_COMPILE=t450-); t450-ld is the ld -r wrapper (tools/bin/st20-ldr)
cd $P/bin
ln -sf $P/bin/st20-ldr t450-ld; ln -sf $P/bin/ast t450-as; ln -sf /usr/bin/ar t450-ar
ln -sf /bin/true t450-nm; ln -sf /bin/true t450-strip
echo TOOLCHAIN OK
