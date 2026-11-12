#!/bin/sh
# Run a command in the st20-build container for the 2.4 port (from uClinux/2.4/).
# /work = this directory; /work/tools = the 2.0 toolchain (gcc-2.7.2-t800, ttools), whose
# installed paths are /work/tools/...; /work/2.0 = the 2.0 tree (user space, romfs tools);
# /work/port-tools = this directory's tools/ (hidden under /work/tools).
cd "$(dirname "$0")"
exec docker run --rm --platform linux/386 -v "$PWD":/work -v "$PWD/../2.0/tools":/work/tools \
  -v "$PWD/../2.0":/work/2.0 -v "$PWD/tools":/work/port-tools st20-build "$@"
