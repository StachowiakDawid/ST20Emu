#!/bin/bash
# Host side of the image build (from ucLinux/, after the Docker builds of the kernel,
# user/build-libc.sh and user/build-user.sh): bFLT programs -> romfs -> 2 MB DSR9500 flash.
# Root file system: /bin/busybox with every applet as a romfs hard link (busybox.links,
# written by build-user.sh), /sbin/init = busybox init (/etc/inittab, /etc/rc), sash
# kept as /bin/sash.
set -e
cd "$(dirname "$0")/.."
python3 tools/mkflat.py user/build/init user/build/init.bflt
python3 tools/mkflat.py user/build/sh user/build/sh.bflt 16384
python3 tools/mkflat.py user/build/sigtest user/build/sigtest.bflt
python3 tools/mkflat.py user/build/stattest user/build/stattest.bflt
python3 tools/mkflat.py user/build/busybox user/build/busybox.bflt 32768
links=()
for l in $(cat user/build/busybox.links); do
  links+=("link:$l:/bin/busybox")
done
python3 tools/mkromfs.py user/build/romfs.img dir:/dev dir:/proc dir:/tmp \
  dir:/bin dir:/sbin dir:/usr/bin dir:/usr/sbin \
  chr:/dev/console:5:1 chr:/dev/ttyS0:4:64 chr:/dev/tty:5:0 chr:/dev/null:1:3 \
  chr:/dev/zero:1:5 chr:/dev/panel:60:0 \
  blk:/dev/ram0:1:0 blk:/dev/ram1:1:1 blk:/dev/rom0:31:0 \
  file:/bin/busybox:user/build/busybox.bflt:x "${links[@]}" \
  file:/bin/sash:user/build/sh.bflt:x file:/bin/hello:user/build/init.bflt:x \
  file:/bin/sigtest:user/build/sigtest.bflt:x \
  file:/bin/stattest:user/build/stattest.bflt:x \
  file:/etc/inittab:user/rootfs/etc/inittab file:/etc/rc:user/rootfs/etc/rc:x \
  file:/etc/profile:user/rootfs/etc/profile \
  file:/lib/liberror.txt:src/uclinux-dist/lib/libc/error/liberror.txt
python3 tools/mkrom-dsr9500.py src/uclinux-dist/linux-2.0.x/linux.bin user/build/romfs.img \
  user/build/bootinit.bin dsr9500-uclinux.bin
