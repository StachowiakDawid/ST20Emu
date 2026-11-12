#!/bin/sh
# rebuild cc1 + libgcc.a after changes in src/gcc-2.7.2 and install into tools/
set -e
P=/work/tools; export PATH=$P/bin:$PATH
cd /work/src/gcc-build
rm -f *.o libgcc.a libgcc2.a stmp-multilib fp-bit.c dp-bit.c
make xgcc cc1 cpp libgcc.a specs LANGUAGES=c CC='gcc -fwrapv -fno-strict-aliasing' CFLAGS='-O1 -w' LIBGCC1_TEST= > /work/src/gcc-make.log 2>&1 || { grep -iE 'error' /work/src/gcc-make.log | head -20; tail -15 /work/src/gcc-make.log; exit 1; }
L=$P/lib/gcc-lib/t450/2.7.2-t800.12
cp cc1 cpp libgcc.a specs $L/; cp -r include/* $L/include/; cp ../gcc-2.7.2/config/t800/float.h $L/include/ 2>/dev/null || true
cp xgcc $P/bin/t450-gcc
echo GCC OK
