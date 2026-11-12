#!/bin/bash
# relink the kernel with LINKFLAGS given as $1 (test helper)
export PATH=/work/tools/bin:$PATH
cd /work/src/uclinux-dist/linux-2.0.x
L=/work/tools/lib/gcc-lib/t450/2.7.2-t800.12/libgcc.a
st20-ldr $1 arch/st20/kernel/head.o init/main.o init/version.o arch/st20/kernel/kernel.o arch/st20/mm/mm.o kernel/kernel.o fs/fs.o ipc/ipc.o net/network.a mmnommu/mm.o fs/filesystems.a drivers/block/block.a drivers/char/char.a drivers/net/net.a lib/lib.a arch/st20/lib/lib.a $L -o linux
