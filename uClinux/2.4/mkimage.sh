#!/bin/bash
# Builds the DSR9500 flash image dsr9500-uclinux24.bin (run on the host, from
# uClinux/2.4/; extra arguments go to the kernel make, e.g. -j6):
#   1. kernel in the container (make linux.bin: make, st20-ld, crossload)
#   2. root romfs build/romfs.img: this port's user space (uClibc 0.9.26 + busybox 1.00
#      and test programs, bFLT, built by user/build-user.sh, run first) with rootfs/etc
#   2b. kernel modules modules/*.c (linked against this kernel's linux.map by
#      modules/build.sh, made relocatable by modules/mkmod.py) -> /lib/modules/NAME.mod
#   3. flash image: tools/mkrom-dsr9500.py with the 2.0 port's boot init blob, layout =
#      the kernel's MTD partitions (README.md "Flash image").  MKROM_DATA=OLD.bin keeps the
#      JFFS2 partition (/etc/config) of an earlier image, e.g. one saved from st20emu.
set -e
cd "$(dirname "$0")"
U=../2.0/user/build
./dbuild.sh /work/kbuild.sh "$@" linux.bin
K=src/uclinux-dist/linux-2.4.x
mkdir -p build
# --- kernel modules (after every kernel build: a module is refused by any other kernel)
mods=()
mkdir -p build/mod
python3 tools/mkptitc.py build/mod/pti_tc.h
names=$(cd modules && ls *.c | sed 's/\.c$//')
./dbuild.sh /work/modules/build.sh $names >/dev/null
for m in $names; do
  python3 modules/mkmod.py build/mod $m $K/linux.bin $K/linux.map build/mod/$m.mod
  mods+=("file:/lib/modules/$m.mod:build/mod/$m.mod")
done
# --- user space (user/build-user.sh: uClibc + busybox 1.00; fbtest/evtest from tools/test,
#     nettest/modtool from user/net)
B=user/build
[ -f $B/busybox.bflt ] || user/build-user.sh
links=()
for l in $(cat $B/busybox.links); do
  links+=("link:$l:/bin/busybox")
done
python3 ../2.0/tools/mkromfs.py build/romfs.img dir:/dev dir:/dev/input dir:/proc dir:/tmp \
  dir:/bin dir:/sbin dir:/usr/bin dir:/usr/sbin dir:/etc dir:/etc/config dir:/lib dir:/lib/modules \
  chr:/dev/console:5:1 chr:/dev/ttyS0:4:64 chr:/dev/tty:5:0 chr:/dev/null:1:3 \
  chr:/dev/zero:1:5 chr:/dev/panel:60:0 chr:/dev/fb0:29:0 chr:/dev/input/event0:13:64 \
  chr:/dev/mtd0:90:0 chr:/dev/mtd1:90:2 chr:/dev/mtd2:90:4 chr:/dev/mtd3:90:6 \
  chr:/dev/mtdr0:90:1 chr:/dev/mtdr1:90:3 chr:/dev/mtdr2:90:5 chr:/dev/mtdr3:90:7 \
  blk:/dev/ram0:1:0 blk:/dev/ram1:1:1 \
  blk:/dev/mtdblock0:31:0 blk:/dev/mtdblock1:31:1 blk:/dev/mtdblock2:31:2 \
  blk:/dev/mtdblock3:31:3 \
  chr:/dev/hello:61:0 \
  dir:/dev/dvb dir:/dev/dvb/adapter0 chr:/dev/dvb/adapter0/video0:212:0 \
  chr:/dev/dvb/adapter0/audio0:212:1 chr:/dev/dvb/adapter0/frontend0:212:3 \
  chr:/dev/dvb/adapter0/demux0:212:4 chr:/dev/dvb/adapter0/dvr0:212:5 \
  chr:/dev/ptyp0:2:0 chr:/dev/ptyp1:2:1 chr:/dev/ptyp2:2:2 chr:/dev/ptyp3:2:3 \
  chr:/dev/ttyp0:3:0 chr:/dev/ttyp1:3:1 chr:/dev/ttyp2:3:2 chr:/dev/ttyp3:3:3 \
  file:/bin/busybox:$B/busybox.bflt:x "${links[@]}" \
  file:/bin/uctest:$B/uctest.bflt:x file:/bin/retest:$B/retest.bflt:x \
  file:/bin/sigtest:$B/sigtest.bflt:x file:/bin/stattest:$B/stattest.bflt:x \
  file:/bin/fbtest:$B/fbtest.bflt:x file:/bin/evtest:$B/evtest.bflt:x \
  file:/bin/nettest:$B/nettest.bflt:x file:/bin/modtool:$B/modtool.bflt:x \
  link:/sbin/insmod:/bin/modtool link:/sbin/rmmod:/bin/modtool link:/sbin/lsmod:/bin/modtool \
  file:/bin/dvbtool:$B/dvbtool.bflt:x link:/bin/scan:/bin/dvbtool link:/bin/zap:/bin/dvbtool \
  link:/bin/tv:/bin/dvbtool link:/bin/femon:/bin/dvbtool \
  "${mods[@]}" \
  file:/etc/inittab:rootfs/etc/inittab file:/etc/rc:rootfs/etc/rc:x \
  file:/etc/profile:rootfs/etc/profile \
  file:/etc/passwd:rootfs/etc/passwd file:/etc/group:rootfs/etc/group \
  file:/etc/hosts:rootfs/etc/hosts file:/etc/services:rootfs/etc/services \
  file:/etc/protocols:rootfs/etc/protocols file:/etc/inetd.conf:rootfs/etc/inetd.conf
# --- end user space
python3 tools/mkrom-dsr9500.py ${MKROM_DATA:+--data "$MKROM_DATA"} \
  $K/linux.bin build/romfs.img $U/bootinit.bin dsr9500-uclinux24.bin
