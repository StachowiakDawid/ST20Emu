#!/bin/bash
# Kernel build inside st20-build (via ./dbuild.sh): kbuild.sh [make targets...]
export PATH=/work/tools/bin:$PATH
cd /work/src/uclinux-dist/linux-2.4.x
exec make "$@"
