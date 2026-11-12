#!/bin/bash
# Kernel build inside st20-build: kbuild.sh [make targets...]
export PATH=/work/tools/bin:$PATH
cd /work/src/uclinux-dist/linux-2.0.x
exec make "$@"
