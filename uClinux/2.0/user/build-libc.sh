#!/bin/bash
# Builds uC-libc (src/uclinux-dist/lib/libc, MACHINE=st20) in the st20-build container:
#   docker run --rm --platform linux/386 -v $PWD:/work st20-build /work/user/build-libc.sh [make args]
export PATH=/work/tools/bin:$PATH
L=/work/src/uclinux-dist/lib/libc
cd $L
# CFLAGS from the environment, so the Makefiles' own "CFLAGS += ..." still apply
export CFLAGS="-O2 -mt450 -Wall -D__restrict= -I$L/include"
exec make MACHINE=st20 LINUXDIR=linux-2.0.x CC=t450-gcc AS=ast AR=ar ARFLAGS=r RANLIB=true "$@"
