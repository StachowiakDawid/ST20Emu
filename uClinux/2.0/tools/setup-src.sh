#!/bin/bash
# Recreates the ignored dl/ and src/ trees from upstream + patches/ (run on the host, from
# ucLinux/). Then build the toolchain and kernel in Docker, see README.md.
set -e
cd "$(dirname "$0")/.."
top=$PWD
mkdir -p dl src
B=https://www.wotug.org/parallel/transputer/software/compilers/gcc/pereslavl
[ -f dl/gcc-2.7.2.tar.gz ] || curl -sSfL -o dl/gcc-2.7.2.tar.gz https://ftp.gnu.org/old-gnu/gcc/gcc-2.7.2.tar.gz
[ -f dl/gcc-2.7.2-t800.12.dif.gz ] || curl -sSfL -o dl/gcc-2.7.2-t800.12.dif.gz $B/gcc-2.7.2/gcc-2.7.2-t800.12.dif.gz
[ -f dl/ttools-2.0beta2.tar.gz ] || curl -sSfL -o dl/ttools-2.0beta2.tar.gz $B/ttools/ttools-2.0beta2.tar.gz

cd src
if [ ! -d gcc-2.7.2 ]; then
  tar xzf ../dl/gcc-2.7.2.tar.gz && chmod -R u+w gcc-2.7.2
  (cd gcc-2.7.2 && gzip -dc ../../dl/gcc-2.7.2-t800.12.dif.gz | patch -p1 -s)
  patch -p0 -s < ../patches/gcc-2.7.2-st20.diff
fi
if [ ! -d ttools-2.0beta2 ]; then
  tar xzf ../dl/ttools-2.0beta2.tar.gz
  patch -p0 -s < ../patches/ttools-2.0beta2.diff
fi
if [ ! -d uclinux-dist ]; then
  git clone -q --depth 1 --filter=blob:none --sparse \
      https://github.com/neozeed/uClinux-dist-20040218 uclinux-dist
  git -C uclinux-dist sparse-checkout set linux-2.0.x lib/libc user/sash user/busybox
  git -C uclinux-dist apply "$top/patches/linux-2.0.x-st20.diff"
  cp "$top/patches/linux-2.0.x.config" uclinux-dist/linux-2.0.x/.config
fi
echo "src/ ready"
