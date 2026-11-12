#!/bin/bash
# Recreates the ignored src/ tree from upstream + patches/ (run on the host, from
# uClinux/2.4/): sparse clone of uClinux-dist-20040218 (linux-2.4.x only), the ST20 port
# (patches/linux-2.4.x-st20.diff, incl. the new arch/st20 and include/asm-st20) and the
# kernel .config; uClibc 0.9.26 and busybox 1.00 from dl/ (downloaded) + their patches.
# The toolchain is the 2.0 port's (../2.0/tools, see ../2.0/README.md);
# then build with ./mkimage.sh, see README.md.
set -e
cd "$(dirname "$0")/.."
top=$PWD
mkdir -p src logs
cd src
if [ ! -d uclinux-dist ]; then
  git clone -q --depth 1 --filter=blob:none --sparse \
      https://github.com/neozeed/uClinux-dist-20040218 uclinux-dist
  git -C uclinux-dist sparse-checkout set linux-2.4.x
  git -C uclinux-dist apply "$top/patches/linux-2.4.x-st20.diff"
  cp "$top/patches/linux-2.4.x.config" uclinux-dist/linux-2.4.x/.config
fi
# user space (user/build-user.sh): uClibc 0.9.26 and busybox 1.00 tarballs in dl/
cd "$top"
mkdir -p dl
[ -f dl/uClibc-0.9.26.tar.bz2 ] ||
  curl -sfL -o dl/uClibc-0.9.26.tar.bz2 https://uclibc.org/downloads/old-releases/uClibc-0.9.26.tar.bz2
[ -f dl/busybox-1.00.tar.bz2 ] ||
  curl -sfL -o dl/busybox-1.00.tar.bz2 https://busybox.net/downloads/busybox-1.00.tar.bz2
if [ ! -d src/uClibc-0.9.26 ]; then
  tar xjf dl/uClibc-0.9.26.tar.bz2 -C src
  (cd src && patch -s -p0 < "$top/patches/uClibc-0.9.26-st20.diff")
  cp patches/uClibc-0.9.26.config src/uClibc-0.9.26/.config
fi
if [ ! -d src/busybox-1.00 ]; then
  tar xjf dl/busybox-1.00.tar.bz2 -C src
  (cd src && patch -s -p0 < "$top/patches/busybox-1.00-st20.diff")
fi
echo "src/ ready; next: ./dbuild.sh /work/kbuild.sh oldconfig dep && ./mkimage.sh -j6"
