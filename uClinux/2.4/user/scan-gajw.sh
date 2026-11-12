#!/bin/bash
# Lists functions that gcc-2.7.2-t800 compiles with a dynamic workspace (gajw): alloca,
# variable-length arrays and calls of a function named "alloca".  That code was broken
# until the gcc fixes of 2026-11-11/12 (logs/gcc-2.7.2-t800-alloca*.c); now informational
# (cc1's "alloca in a loop after a call" warning flags the one case still not handled).
# Runs in the container (../dbuild.sh /work/user/scan-gajw.sh); recompiles the
# sources of the built uClibc and busybox objects with -S (same flags as build-user.sh).
# st20 setjmp.ast (longjmp) uses gajw on purpose and is not scanned.
UC=/work/src/uClibc-0.9.26
BB=/work/src/busybox-1.00
G=/work/tools/lib/gcc-lib/t450/2.7.2-t800.12/include
CC="/work/tools/bin/t450-gcc -B/work/user/bin/ -O2 -fno-builtin -mt450 -w"
scan() {  # FILE CFLAGS...: print FILE:function for every function with gajw
  local f=$1; shift
  $CC "$@" -S "$f" -o - 2>/dev/null | awk -v f="$f" '/^_[A-Za-z0-9_]*:/{fn=$1} /gajw/{print f ": " fn}' | sort -u
}
cd $UC/libc
for o in $(find . -name '*.o'); do
  c=${o%.o}.c; d=$(dirname $o)
  if [ -f $c ]; then scan $c -nostdinc -D_LIBC -I$UC/include -I$d -I$G
  else  # multi-object sources (-DL_name): find the source that has L_name
    n=$(basename $o .o)
    for s in $d/*.c; do grep -q "L_$n\b" $s && { scan $s -nostdinc -D_LIBC -DL_$n -I$UC/include -I$d -I$G; break; }; done
  fi
done
cd $BB
for o in $(find . -name '*.o' -not -path './scripts/*'); do
  c=${o%.o}.c
  [ -f $c ] && scan $c -nostdinc -D_GNU_SOURCE -D__uClinux__ -I/work/user/include -I$UC/include -I$G \
    -Iinclude -Ilibbb -I$(dirname $c)
done
