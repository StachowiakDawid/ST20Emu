#!/bin/bash
# Builds the user programs in the st20-build container; run tools/mkflat.py on the host after.
#   docker run --rm --platform linux/386 -v $PWD:/work st20-build /work/user/build-user.sh
# Each program is linked twice (bases 0 and 0x100000): mkflat.py diffs the images to find
# the words that hold addresses (bFLT relocations).
#   build/init      tiny libc-free test program (user/init.c, user/crt0.ast)
#   build/sh        sash with uC-libc (needs user/build-libc.sh first)
#   build/busybox   busybox 0.60.5 (uClinux-dist user/busybox), config user/busybox-config.h;
#                   "busybox" as argument rebuilds only busybox
set -e
export PATH=/work/tools/bin:$PATH
LIBGCC=$(t450-gcc -print-libgcc-file-name)
LIBC=/work/src/uclinux-dist/lib/libc
SASH=/work/src/uclinux-dist/user/sash
cd /work/user
mkdir -p build

# link NAME CRT0 OBJECTS... : two links + raw images for mkflat.py
link() {
  local name=$1 crt0=$2; shift 2
  for base in 0 0x100000; do
    st20-ldr -T $base -mbuild/$name.$base.map $crt0 "$@" $LIBGCC -o build/$name.$base.lit
    crossload -o build/$name.$base.bin build/$name.$base.lit
  done
}

# DSR9500 boot init blob (clock + SDRAM set-up), run from flash at 0x7fff1000
ast -o build/bootcrt.o boot/bootcrt.s
t450-gcc -O2 -mt450 -Wall -c boot/bootinit.c -o build/bootinit.o
lit -T 0x7fff1000 build/bootcrt.o build/bootinit.o -o build/bootinit.lit
crossload -o build/bootinit.bin build/bootinit.lit

# libc-free init
CFLAGS="-O2 -mt450 -fsigned-char -Wall"
ast -o build/crt0.o crt0.ast
t450-gcc $CFLAGS -c sys.c -o build/sys.o
t450-gcc $CFLAGS -c init.c -o build/init.o
link init build/crt0.o build/init.o build/sys.o

BB=/work/src/uclinux-dist/user/busybox
BBCONF=/work/user/busybox-config.h
UCFLAGS="-O2 -mt450 -Wall -D__restrict= -I$LIBC/include -I/work/user/include -DEMBED"

# busybox: applet sources come from the BB_ names defined in the config (like busybox.sh)
build_busybox() {
  local o=build/busybox flags src f
  mkdir -p $o/libbb
  flags="$UCFLAGS -D__uClinux__ -D_GNU_SOURCE -DUSE_SYSTEM_PWD_GRP -DBB_VER=\"0.60.5\"
         -DBB_BT=\"$(TZ=UTC date -u +%Y.%m.%d-%H:%M%z)\" -DCONFIG_H=\"$BBCONF\" -I$BB"
  src=$(t450-gcc $UCFLAGS -E -dM $BBCONF | sed -n -e '/BB_FEATURE/d' \
        -e 's/^#define BB_\([A-Z0-9_]*\).*/\1/p' | tr A-Z a-z | sort)
  # cc SRC OBJ: compile unless OBJ is newer than SRC and the shared headers
  cc() {
    local h
    if [ -f $2 ]; then
      for h in $1 $BBCONF $BB/busybox.h $BB/applets.h $BB/usage.h $BB/libbb/libbb.h \
               $LIBC/libc.a; do
        [ $h -nt $2 ] && break
        h=
      done
      [ -z "$h" ] && return 0
    fi
    t450-gcc $flags "${@:3}" -c $1 -o $2
  }
  rm -f $o/libbb.a
  for f in $src busybox usage applets; do
    [ -f $BB/$f.c ] || continue
    cc $BB/$f.c $o/$f.o
  done
  # libbb: everything that compiles without networking/loop/module headers; lit pulls
  # only what is referenced out of the archive
  for f in $BB/libbb/*.c; do
    case $(basename $f .c) in
      messages|unarchive|loop|module_syscalls|create_icmp_socket|interface|xgethostbyname|\
      herror_msg|herror_msg_and_die|vherror_msg|syslog_msg_with_name|libc5|unzip|gz_open|\
      read_package_field|mtab) continue;;
    esac
    cc $f $o/libbb/$(basename $f .c).o -I$BB/libbb
  done
  for m in full_version name_too_long omitting_directory not_a_directory memory_exhausted \
           invalid_date invalid_option io_error dash_dash_help write_error too_few_args \
           name_longer_than_foo unknown can_not_create_raw_socket; do
    cc $BB/libbb/messages.c $o/libbb/msg_$m.o -I$BB/libbb -DL_$m
  done
  ar r $o/libbb.a $o/libbb/*.o
  link busybox $LIBC/crt0.o $o/*.o $o/libbb.a $LIBC/libc.a
  # applet paths for mkimage.sh, one per line (as busybox.mkll)
  t450-gcc $UCFLAGS -E -DMAKE_LINKS -include $BBCONF $BB/applets.h | awk '/^[ \t]*LINK/{
    dir=substr($2,8); gsub("_","/",dir); if(dir=="/ROOT") dir=""
    file=$3; gsub("\"","",file); if (file=="busybox") next
    print tolower(dir) "/" file }' > build/busybox.links
}
if [ "$1" = busybox ]; then build_busybox; echo BUSYBOX OK; exit 0; fi

# sash (uC-libc)
if [ -f $LIBC/libc.a ]; then
  mkdir -p build/sash
  # ps.c needs floating point: user/sash-ps-stub.c instead
  for f in sash cmds cmd_uclinux ls hexdump df free hostname date; do
    t450-gcc $UCFLAGS -c $SASH/$f.c -o build/sash/$f.o
  done
  # libsash: utils.c is compiled once per function (-DL_name), like its Makefile does
  for f in intflag modestring timestring isadir copyfile buildname expandwildcards \
           namesort match makeargs makestring chunks expandenvvar; do
    t450-gcc $UCFLAGS -I$SASH -DL_$f -c $SASH/libsash/utils.c -o build/sash/lib_$f.o
  done
  t450-gcc $UCFLAGS -c sash-ps-stub.c -o build/sash/ps.o
  link sh $LIBC/crt0.o build/sash/*.o $LIBC/libc.a
  # test programs
  for p in sigtest stattest; do
    t450-gcc $UCFLAGS -c $p.c -o build/$p.o
    link $p $LIBC/crt0.o build/$p.o $LIBC/libc.a
  done
  build_busybox
fi
echo USER OK
