# uClinux 2.4 on ST20 (st20emu)

Port of uClinux 2.4 (linux 2.4.24-uc0 from uClinux-dist-20040218) to the ST20-C2
(**Samsung DSR9500**, STi5512), the only supported board, tested on st20emu (this repo,
`../..`). It builds on the finished 2.0 port next to it (`../2.0`, see `../2.0/README.md`):
same toolchain, ABI, board, boot loader and most of `arch/st20`; its own user space is
uClibc 0.9.26 + busybox 1.00 (section "User space").

Status (2026-11-11): the kernel boots to the console banner, mounts the romfs root from
flash, runs busybox init, `/etc/rc` (proc, minix RAM disk on /tmp, front panel) and an msh
prompt on ttyS0. Tested: files, hard links, dirs on /tmp, pipes, vfork+exec, background
jobs + ps + kill/killall, Ctrl-C on busy loops and on `sleep`, signal handlers, illegal
instruction trap (SIGILL), /proc (meminfo, interrupts, cpuinfo, pid/status, mounts), vi,
sash, mkfs.minix + mount/umount of a second RAM disk, TV text console, front panel
(`/dev/panel`, keys), IR remote keyboard.

Added later the same day: MTD on the 2 MB flash (`/proc/mtd`: kernel, romfs, jffs2, boot;
the root romfs is now `/dev/mtdblock1`), JFFS2 on `/etc/config` (persists across emulator
runs when the flash is saved, below), `/dev/fb0` on the OSD (640x480, 8/4/2 bpp palettised,
mmap), and the IR remote + front panel as `/dev/input/event0` (input core + evdev) while
they still type into the console. The 512 KB kernel limit is gone (crossload rebuilt).

User space (2026-11-11, later): uClibc 0.9.26 ported (`libc/sysdeps/linux/st20`) and busybox
1.00 (hush with job control as `/bin/sh`, msh, lash; 123 applets then, 131 with networking) replace the 2.0 port's
uC-libc/busybox 0.60.5 in the romfs; init -> rc -> hush prompt, see "User space".

Core kernel features (2026-11-11, from work/core): system call restarting, rt signals with
siginfo, six-argument calls, SIGFPE, ptrace, SysV IPC, freeing init memory.

Networking and modules (2026-11-12, from work/net): TCP/IP (IPv4, TCP, UDP, ICMP, raw and
UNIX sockets) over loopback and SLIP (on ttyS0; tested against a host stack through
st20emu `--serial-tcp`), BSD ptys, busybox ifconfig/route/ping/netstat/nc/telnet/telnetd/
httpd/inetd, and loadable kernel modules (host-linked `.mod` images, `insmod`/`rmmod`/
`lsmod`); the kernel partition grew to 768 KB for it.  User space rebuilt with the fixed
gcc (libm and awk workarounds and `-fno-strength-reduce` gone).  See "Networking and
loadable modules".

Toolchain round (2026-11-12, later): gcc-t800 alloca/inlined-call and libcall fix,
designated-initialiser fix and ttools ast long-string fix installed in `../2.0/tools`
(`../2.0/README.md`); removed with them: uClibc's `__st20_alloca`/`REGEX_MALLOC`/strdupa-off,
the `as` long-string splitters (kernel `-B arch/st20/tools/`, `user/bin/as`),
`CFLAGS_igmp.o`, the named members in `net/ipv4/arp.c`; `CONFIG_MTD_JEDECPROBE` is on
again.  uClibc `vfork` now returns -1 and sets errno; the kernel copies a nested vfork
child's stack too.  Kernel 721,776 bytes, romfs 648,192 bytes.

## Layout in git

Only the port's own files are tracked; `src/` (third-party trees), `dl/` (tarballs),
`logs/`, `build/` and `user/build/` are ignored. Every change to third-party code is in
`patches/`:

| file | what |
| --- | --- |
| `patches/linux-2.4.x-st20.diff` | uClinux-dist `linux-2.4.x` (incl. new `arch/st20`, `include/asm-st20`) |
| `patches/linux-2.4.x.config` | kernel `.config` |
| `patches/uClibc-0.9.26-st20.diff`, `patches/uClibc-0.9.26.config` | uClibc 0.9.26 (`dl/` tarball from uclibc.org) incl. new `libc/sysdeps/linux/st20`, and its `.config` |
| `patches/busybox-1.00-st20.diff` | busybox 1.00 (`dl/` tarball from busybox.net) |
| `user/build-user.sh` | user space build (host; container part through `dbuild.sh`) -> `user/build/*.bflt`, `busybox.links` |
| `user/busybox.config`, `user/busybox-seed.config` | busybox `.config` and the option list it is made from |
| `user/bin/cpp`, `user/bin/t450-ranlib` | cpp 4.3 for t450-gcc (`-B`), no-op ranlib |
| `user/include/stdbool.h`, `user/busybox-libs.mk` | header for gcc 2.7.2, busybox make target for the archives |
| `user/test/` | `uctest.c` (uClibc test), `retest.c` (regex/glob) |
| `user/scan-gajw.sh` | lists functions compiled with a dynamic workspace (alloca/VLA; was needed while gcc-t800's was broken) |
| `tools/userspace.script` | st20emu user-space test script |
| `tools/net.script`, `tools/slip.script`, `tools/slip_host.py` | st20emu networking/modules tests; SLIP test (with `--serial-tcp 3:5555`) and its host end |
| `modules/` | loadable kernel modules: `build.sh` (container: compile + `modlink.sh`), `modlink.sh` (two links against `linux.map`), `mkmod.py` (host: relocatable `.mod`), `hello.c` (sample) |
| `user/net/` | `nettest.c` (loopback/SLIP/ping/ifconfig tests), `modtool.c` (insmod/rmmod/lsmod) |
| `tools/setup-src.sh` | recreates `src/` (sparse clone + patch + config) |
| `tools/mkpatches.sh` | regenerates `patches/` from `src/` (run before committing) |
| `tools/mkrom-dsr9500.py` | flash image builder (2.0's, with this port's MTD layout and `--data`) |
| `tools/smoke.script` | st20emu test script (boot, shell, signals, panel, IR, MTD/JFFS2, fb, input; screenshots to `logs/`) |
| `tools/test/` | kernel test programs `fbtest.c`, `evtest.c` (built with uClibc by `user/build-user.sh`; `build.sh` builds them against 2.0's uC-libc) |
| `tools/addr2sym.py` | address -> nearest symbol in `linux.map` (debugging) |
| `rootfs/etc/` | `rc`, `inittab`, `profile`, `passwd`, `group`, `hosts`, `services`, `protocols`, `inetd.conf` of the root romfs |
| `dbuild.sh CMD...` | runs CMD in the `st20-build` container (`/work` = here, `/work/tools` = `../2.0/tools`, `/work/2.0` = `../2.0`, `/work/port-tools` = this `tools/`) |
| `kbuild.sh [targets]` | kernel make in the container |
| `mkimage.sh [make args]` | kernel + modules + romfs (runs `user/build-user.sh` if `user/build` is empty) + flash image -> `dsr9500-uclinux24.bin` (`MKROM_DATA=FILE` keeps a saved JFFS2 partition) |

`dbuild.sh` mounts `../2.0/tools` over `/work/tools`: inside the container `tools/` is the
2.0 toolchain, not this directory's scripts (which run on the host).

## Build and run

Needs the 2.0 port built first (its toolchain in `../2.0/tools` and `bootinit.bin` in
`../2.0/user/build`, see `../2.0/README.md`). The toolchain's `crossload` must be the one without the 512 KB limit
(`../2.0/patches/ttools-2.0beta2.diff`, 2026-11-11; rebuild just crossload with the
command in `../2.0/build-toolchain.sh` if an older one is installed: it fails with "area
overflowed").

```sh
tools/setup-src.sh                                   # once: src/ from upstream + patches
./dbuild.sh /work/kbuild.sh oldconfig dep            # once
user/build-user.sh                                   # uClibc + busybox, ~20 min clean (qemu)
./mkimage.sh -j6                                     # ~1 min clean -> dsr9500-uclinux24.bin
../../builddir-release/st20emu --soc sti5512 --board dsr9500 --firmware dsr9500-uclinux24.bin
# interactive: g to run; or scripted:
../../builddir-release/st20emu --soc sti5512 --board dsr9500 --firmware dsr9500-uclinux24.bin \
    --script tools/smoke.script
# or as a serial terminal: --console 3
```

Script lines: `run N` (instructions; ~400M to the prompt), `asc 3 TEXT` (types TEXT + newline
on the console; a literal 0x03 byte in the line is Ctrl-C), `ir 0x6c 0x73 0x8d` (IR keys,
here `ls` Enter), `fp power`, `led`, `screen FILE.bmp` (TV picture).

## Flash image

2 MB at 0x7fe00000, 64 KB sectors (the real chip's main sectors; the top 64 KB is the boot
block). The partitions are the kernel's MTD partitions
(`drivers/mtd/maps/dsr9500-flash.c`, `include/asm-st20/board.h` `ST20_FLASH_*`) and
`tools/mkrom-dsr9500.py` places the images the same way:

| flash | offset | size | MTD | content |
| --- | --- | --- | --- | --- |
| 0x7fe00000 | 0x000000 | 768 KB | mtd0 `kernel` | kernel image (text + init + data; bss is cleared by head.ast; copied to 0xc0001000 at boot); now 721,776 bytes (705 KB) |
| 0x7fec0000 | 0x0c0000 | 704 KB | mtd1 `romfs` | root romfs (`/dev/mtdblock1`, read-only); now 648,192 bytes (633 KB) |
| 0x7ff70000 | 0x170000 | 512 KB | mtd2 `jffs2` | JFFS2 (8 erase blocks), mounted on `/etc/config` by `/etc/rc`; erased (0xff) in a new image |
| 0x7fff0000 | 0x1f0000 | 64 KB | mtd3 `boot` (read-only) | loader at 0x7fff0000 (EMI pokes, boot init blob `../2.0/user/build/bootinit.bin` at 0x7fff1000, copy, jump), `ldc; gcall` at 0x7fffffe0, reset entry 0x7ffffffe |

mkrom-dsr9500.py refuses a kernel or romfs bigger than its partition. The boundary moved
on 2026-11-12 from 704/768 KB to 768/704 KB (kernel/romfs) for networking; a flash saved
from an older image has its romfs at 0x7feb0000 and does not boot with a new kernel (the
JFFS2 and boot partitions did not move, so `MKROM_DATA=old.bin` still works). JFFS2 (this 2.4 one)
keeps 5 erase blocks free for writes and garbage collection
(`JFFS2_RESERVED_BLOCKS_WRITE`): a 5-block partition could not store anything, 8 blocks hold
about 188 KB (`df /etc/config`).

The root romfs (`build/romfs.img`, `mkimage.sh`) holds this port's bFLT programs from
`user/build` (busybox 1.00 with every applet as a hard link, uctest, retest, sigtest,
stattest, fbtest, evtest, nettest, modtool with its `/sbin/insmod`, `rmmod`, `lsmod` links;
633 of 704 KB), `/lib/modules/*.mod` and `rootfs/etc`, plus /dev nodes for `mtd0-3`/`mtdr0-3` (90,0-7),
`mtdblock0-3` (31,0-3), `fb0` (29,0), `input/event0` (13,64), `hello` (61,0, the sample
module), `ptyp0-3`/`ttyp0-3` (2,0-3 / 3,0-3). The 2.0 image
`../2.0/user/build/romfs.img` (uC-libc, busybox 0.60.5) boots as well (no `/etc/config`).

### Saving the flash in st20emu (JFFS2 persistence)

st20emu cannot save memory on exit, but `dump FILE ADDRESS LENGTH` writes memory to a raw
file and a 2 MB flash file boots with `--firmware` like the built image:

```sh
# end of a session script (after the writes; JFFS2 on NOR writes through at once, a
# `sync` or `umount /etc/config` before is still good practice)
dump flash.bin 7fe00000 200000            # whole flash
# or just the partition:  dump jffs2.bin 7ff70000 80000
../../builddir-release/st20emu --soc sti5512 --board dsr9500 --firmware flash.bin ...
MKROM_DATA=jffs2.bin ./mkimage.sh -j6     # new kernel/romfs, same /etc/config
```

## Design (differences from the 2.0 port)

Everything not listed here works as in `../2.0/README.md`: one low-priority ST20 process
for all of Linux, tick and preemption from a high-priority process through the shadow
registers (`time.c`), user code preempted only, INC/INL interrupts with fast high-priority
top halves (`irq.c`), error traps (`traps.c`), signal frames below Wptr with
`st20_sigtramp` (`signal.c`), system calls through the vector word at 0xc0000000
(`entry.ast`, `syscalls.c`), I-cache on, console/ttyS0 on ASC3, OSD, panel, IR.

- Toolchain: the 2.0 port's gcc-2.7.2-t800 (`t450-gcc`), ast, lit, crossload (with the
  fixes this port's bugs led to, `../2.0/README.md` "Toolchain fixes 2026-11-11/12").
  gcc-t800 accepts GNU `field:` and C99 `.field =` / `[i] =` initialisers, so 2.4's
  designated initialisers needed no rewriting; anonymous unions are not supported but
  are not used by the configured code.
- `arch/st20/Makefile`: no `-fno-strict-aliasing` (unknown to gcc 2.7.2), no `-fno-common`
  (section "Networking and loadable modules", kernel size), `-D__linux__` (gcc-t800 defines only
  `__transputer__`; coda.h and others test `__linux__`). `init/main.c`'s "GCC too old"
  error is a warning for ST20.
- No named sections in lit: `__init`, `__initdata`, `__exit*`, `__setup`/`__initcall`
  section attributes are empty for `CONFIG_ST20` (`include/linux/init.h`; nothing is freed
  after init). `__initcall(fn)` defines a global `initcall_t __st20_ic__BASE__LINE__fn`,
  `__setup(str, fn)` a global `struct kernel_param __st20_su__BASE__LINE__fn` (BASE =
  KBUILD_BASENAME). The kernel link (`arch/st20/tools/st20-ld`, the `LD` of the build)
  lists them with litdump in link order (object order, then line), writes
  `st20_tables.c` (`st20_initcall_table[]`, `st20_setup_table[]`, null-terminated; a copy is
  left in the kernel tree), compiles it and links it after `head.o`. The references also
  pull archive members reachable only through an initcall (file systems, drivers).
  `st20-ld` is otherwise the 2.0 `st20-ldr` (`ld -r` = ar archive, nested archives
  flattened) and ignores `--start-group`/`--end-group`.
- Tasks: 2.4's 8 KB `task_union` (task struct + kernel stack). `current` is a global
  (`_current_task`, like h8300), so `init_task_union` needs no 8 KB alignment;
  `head.ast` starts `start_kernel` on its stack. `switch_to(prev, next, last)` does
  cli, sets `current`, `st20_kstack` (= next + 8 KB) and `st20_last_task`, then
  `st20_resume`; new tasks (`st20_ret_from_fork_c`, `st20_kthread_c`) call
  `schedule_tail` and sti. User vs kernel at preemption: Wptr inside the current task's
  kernel stack = kernel.
- fork = vfork: `CLONE_VFORK | CLONE_VM` (2.4 nommu semantics; exec gives the child a
  new mm), with 2.0's copy of the parent's user stack for the child (gcc-t800 shares
  workspace slots, see 2.0). Kernel threads: `do_fork` then a hand-made resume frame
  (`arch_kernel_thread`).
- Interrupt work at low priority (`st20_preempt_c`) runs inside `irq_enter/irq_exit`,
  then softirqs (`do_softirq`, timers/tasklets/bottom halves), then schedule() and signal
  delivery for preempted user code. The front panel hooks into the tick through
  `st20_tick_hook`/`st20_tick_lp_hook` (`time.c`).
- Headers: `include/asm-st20` started from 2.4 `asm-h8300` (m68k ABI numbers, the same as
  the 2.0 port's), with all assembly replaced by C: cli-based atomics, bitops (little
  endian, ext2/minix = native), semaphores (h8300's generic `semaphore.c`), delay, csum;
  `byteorder.h` little endian; 2.0's `unaligned.h`, `board.h`, `st20asc.h`, fonts/IR keys.
  `ioremap` and friends are identity macros; `__pa`/`__va` are casts.
- System calls: table generated from h8300's 2.4 `syscalls.S` (`syscalltab.c`, 223
  entries; 16-bit uid calls 0-163 as uC-libc expects, 109 = old `uname`). `sys_sigaction`
  takes the old one-word-mask struct (uC-libc); `sigsuspend`/`rt_sigsuspend` deliver
  with the waiting mask and restore the old one after the handler; `ptrace` is ENOSYS.
- bFLT loader (`fs/binfmt_flat.c`): no room for shared-library data pointers in front of
  data (`FLAT_DATA_GAP` 0 for ST20; gcc-t800 addresses data PC-relative from the text, so
  data must follow text exactly), and the start-up stack is `argc, argv[], 0, envp[], 0`
  without argv/envp pointers (`flat_argvp_envp_on_stack()` 0) as the 2.0 crt0 expects.
  Relocated words are big-endian in the file (the loader applies ntohl).
- `/dev/console` is ttyS0 (the printk console's device); the TV console mirrors printk
  and ttyS0 output (in 2.0 /dev/console was the TV). The serial driver is started from
  `tty_init` like in 2.0.
- `copy_mount_options` copies strings only (up to NUL), not a whole page: without an MMU
  the page may run past RAM, and st20emu stops on reads of never-written memory.
- `/dev/panel` ignores zero-length writes (uC-libc's fflush writes 0 bytes; 2.4 passes
  them to the driver, 2.0 did not).
- The TV console's 77 KB field buffers come from the boot allocator (`st20_osd_alloc` in
  `setup_arch`), keeping them out of the kernel image.
- Root file system: romfs on the MTD `romfs` partition through `mtdblock_ro`
  (`ROOT_DEV` 31,1; blkmem is off since it used the same major). The built-in command line
  is `rootfstype=romfs`: mount_root otherwise tries every file system on the root device,
  and JFFS2 comes first, scans the romfs as a broken JFFS2 and would erase it if it found
  a single valid node (it "cowardly refuses" only because it finds none).
- MTD map (`drivers/mtd/maps/dsr9500-flash.c`): the DSR9500's original loader accepts
  AMD/Fujitsu/Samsung/SST parts and programs them with the AMD command set
  (Decompilation/DSR9500 `src/hw.c` `flash_detect`/`flash_write`), so real hardware is
  probed with `cfi_probe`, then `jedec_probe` (x16, `cfi_cmdset_0002`), then mapped
  read-only (`map_rom`). st20emu models no flash chip for `--board dsr9500` (the 6300 and
  DSR7000 boards have an AMD model, `src/board/AmdFlash.cpp`): the array is plain memory,
  so a probe's command writes would land in the kernel partition. Before any probe the
  driver stores 0xf0f0 (AMD read/reset, harmless on a chip) into one flash word: if it
  reads back the word is restored and `map_ram` drives the flash (erase = fill 0xff) with
  64 KB erase blocks. `map_ram` did not set `MTD_ERASE_DONE` before its callback, so JFFS2
  marked every erased block bad (fixed in `drivers/mtd/chips/map_ram.c`). Nothing else
  writes the flash; a dumped flash differs from the image only in the JFFS2 partition.
  The CFI/JEDEC path is untested (no hardware, no emulator model).
- JFFS2 (2.4.24's, with zlib deflate/inflate, rtime, rubin) on mtd2. Erased blocks get
  their clean markers from `write_super` (kupdated, every 5 s) and the `jffs2_gcd_mtd2`
  thread; both run.
- `/dev/fb0` (`drivers/video/st20fb.c`): 640x480 palettised, 8 bpp (default), 4 or 2 bpp
  (`FBIOPUT_VSCREENINFO`), packed pixels, leftmost pixel in the high bits, mmap-able, on
  the TV console's window of the PAL picture. The STi5512 OSD (datasheet 21.5) has no line
  pitch: a block is header + optional palette + contiguous bitmap, one list per field. So
  every frame buffer line is a one-line block: `line_length` (8 bpp 768, 4 bpp 384, 2 bpp
  256 bytes, multiples of 128 for the 16-word aligned links) holds the line's 8-byte header
  and its pixels; `smem_start` is line 0's pixels, and line y+1's header sits in the last 8
  bytes of line y's padding. The top field's list links lines 0, 2, 4..., the bottom
  field's 1, 3, 5...; each starts with a hidden (S=1) two-pixel block on the field line
  above the picture carrying the palette (14-bit colour: 6-bit Y, 4-bit Cb/Cr, from the
  RGB colour map by BT.601), which the line blocks reuse (P=1). The 8 bpp default palette is
  xterm's 256 (16 VGA colours, 6x6x6 cube, grey ramp). A program that clears all of
  `smem_len` also clears the headers: a timer rebuilds them every 100 ms while the frame
  buffer is shown (tested with `dd if=/dev/zero of=/dev/fb0`). The 361 KB buffer comes
  from bootmem (OSD links reach only the low 4 MB of SDRAM with OSD_CFG.LLG=0). The first
  block of a list must be 256-byte aligned: OSD_TOP/OSD_BOT count 256-byte units.
- Console and frame buffer: no fbcon (no virtual terminals, `CONFIG_VT` off). The TV text
  console (`osd.c`) keeps its own two 2 bpp field blocks and keeps drawing into them; the
  OSD shows the frame buffer from the first open of `/dev/fb0` to the last close, then
  the text console again (`st20_osd_select`). fbmem/fbcmap/modedb are built without VT
  (`drivers/video/Makefile`, top `Makefile`, `drivers/Makefile`; fbmem.c's con2fb/fbcon
  parts are `#ifdef CONFIG_VT`, `PROC_CONSOLE` is -1).
- `mmap` of a character device with `MAP_SHARED|PROT_WRITE` is allowed
  (`mmnommu/mmap.c`; it refused all writable shared file mappings, but a device's
  `f_op->mmap` maps its memory directly).
- Input (`arch/st20/kernel/keys.c`): one input device "DSR9500 IR remote + front panel"
  (`/dev/input/event0`, evdev). IR: the remote's own keys (digits, KEY_POWER, arrows,
  KEY_PAGEUP/DOWN, KEY_EXIT, OK = KEY_ENTER, KEY_MENU), NEC 0x20-0x7e as the US-keyboard
  key with KEY_LEFTSHIFT, 0x81-0x9a KEY_LEFTCTRL + letter, Backspace/Tab/Enter/Esc,
  0xa0-0xa9 navigation keys (the same command table as the typing keymap). NEC has no
  release: a key is released 150 ms after its last frame; repeat frames keep it down and
  after three report value 2. Front panel: KEY_UP, KEY_DOWN, KEY_POWER with real press
  and release (panel.c now queues releases too). Both still type into ttyS0 as before.
  This input.h has no KEY_OK, MSC_SCAN or BUS_HOST.
- crossload's 512 KB `MAX_IMAGE_SIZE` is 8 MB (`../2.0/patches/ttools-2.0beta2.diff`);
  its output for the 2.0 kernel is byte-identical.

## User space

uClibc 0.9.26 and busybox 1.00 (uClinux-dist-20040218 has neither: no `lib/uClibc`, and
its `user/busybox` is 0.60.5, the 2.0 port's), both from tarballs in `dl/`
(`tools/setup-src.sh`), unpacked in `src/` and patched (`patches/`). Built by
`user/build-user.sh` (host script; the compile/link part runs in the container, then
`../2.0/tools/mkflat.py` writes the bFLTs, as in 2.0): `user/build-user.sh [uclibc|busybox]`.
Same ABI as 2.0 (system calls through the vector at 0xc0000000, bFLT rev 4 RAM, 16-bit
short, arguments in A/B/C); the 2.0 user space still runs on this kernel.

### uClibc for st20

- `extra/Configs/Config.st20` (no MMU, `_` symbol prefix, no shared libraries, no PIC, not
  ELF), `TARGET_st20` in `Config.in`, `.config` = `patches/uClibc-0.9.26.config`: little
  endian, soft float (libgcc's fp-bit; no `-msoft-float`), libm, no threads, no LFS, no
  locale/wchar/IPv6/RPC, `malloc` (the no-MMU mmap heap), regex, glob, ftw, 1 KB stdio
  buffers, `KERNEL_SOURCE` = this kernel tree (headers linked into `include/asm`,
  `include/linux`).
- `libc/sysdeps/linux/st20/`: `crt0.ast` (envp from argc, `__uClibc_main(argc, argv,
  envp)`; segment symbols for mkflat.py as 2.0's crt0), `setjmp.ast` (`__sigsetjmp`,
  `setjmp`, `_setjmp` save the caller's Wptr + return Iptr and call `__sigjmp_save`;
  `__longjmp`; common `longjmp.c` restores the mask), `vfork.ast` (`__NR_vfork` with
  gcall from its own 4-word frame, whose word 0 is the stub's continuation; the kernel
  returns there like `ret` from that frame, i.e. with Wptr back at the caller's frame, where
  the result is checked: -4095..-1 sets errno and returns -1; `fork` = `vfork`),
  `bits/` from h8300 (m68k kernel ABI: `kernel_stat`, `kernel_types`,
  fcntl, mman, resource) with little-endian `endian.h`, `bits/setjmp.h`, `sys/ucontext.h`.
- System calls are the kernel's own `_syscallN` macros (`asm/unistd.h`, through the vector,
  like h8300); `sigaction` uses `rt_sigaction` (kernel struct = handler, flags, restorer,
  mask; signal returns go through the kernel's `st20_sigtramp`, no restorer needed); stat
  through `kernel_stat`; mmap through `old_mmap` (struct of 6 words).
- `include/features.h`: for `__transputer__`, `weak_alias`/`strong_alias` are ast
  assignments (`.globl _alias; _alias = _name`; lit has no weak symbols and gcc 2.7.2 no
  alias attribute) plus a static pointer to NAME (so gcc emits NAME when it is a
  `static inline` syscall stub); `weak_function` is empty. Where uClibc tests a weak
  symbol for NULL (`_stdio_init`, `__errno_location`) the strong one is linked.
- `Rules.mak` st20: `-O2 -mt450` (`-fno-strength-reduce` until 2026-11-12), no `-fno-strict-aliasing`, gcc's
  include dir from `-print-libgcc-file-name` (no `-print-search-dirs` in 2.7.2), no strip;
  `CC=t450-gcc -B/work/user/bin/` (cpp below; until 2026-11-12 also `user/bin/as`, an `as`
  wrapper for ttools ast's long-string overflow, fixed in ast), no-op `t450-ranlib`. Not `make -j`: every
  directory adds its objects to `libc.a` with ar, parallel ars lose members.
- Preprocessor: `user/bin/cpp` makes the 2.7.2 driver use the container's cpp 4.3
  (`-undef`, the driver's 2.7.2-t800 `-D`s, line-marker flag 4 dropped): cccp 2.7.2 does
  `#if` arithmetic in 32 bits and rejects `LL` constants there ("two `l's in integer
  constant"), which uClibc's stdio/stdlib/stdint.h use everywhere.
- alloca: `<alloca.h>` is upstream's again (`__builtin_alloca`), strdupa/strndupa are on,
  regex uses alloca (no `REGEX_MALLOC`) since 2026-11-12. Until then `alloca` was a
  malloc-based `__st20_alloca` (`st20/alloca.c`, deleted) because gcc-2.7.2-t800's alloca
  was broken (section "gcc-2.7.2-t800 bugs found"): first the block overlapped the Wreg
  save slot, then calls from inlined functions (every uClibc system call is an inline
  `_syscallN` through the vector: `getdents` passed garbage, hush died in calloc) and
  library calls still ran on the old workspace (`logs/gcc-2.7.2-t800-alloca-gcall.c`).
  One case gcc still does not handle: a call expanded before a function's first alloca
  inside a loop that contains the alloca runs on the normal workspace on the next
  iteration and overwrites the earlier blocks; cc1 warns ("alloca in a loop after a
  call"). In uClibc that was `glob_in_dir` (readdir/fnmatch, then alloca per match): it
  now starts with an `alloca(sizeof(int))` for `__transputer__`; busybox's only warning
  (httpd `handleIncoming`) is a `do { } while (0)`, harmless. User code is still compiled
  with `-fno-builtin` (other builtins untested). `user/scan-gajw.sh` lists the functions
  with a dynamic workspace (alloca/VLA), now for information only.
- Other source fixes: `libc/unistd/Makefile` built `__exec_alloc` (exec*'s mmap'd argv)
  only WITH an MMU (inverted test). (libm `k_tan.c`/`s_atan.c` had their compares moved
  before the polynomial for a cc1 abort; upstream again since the gcc fix.)
- Link (`build-user.sh`): `lit -T 0` and `-T 0x100000` of `crt0.o`, objects, `libm.a`,
  `libc.a`, and a copy of libgcc.a without `_exit.o`: lit loads every archive member that
  defines a needed symbol, even one already defined (so libgcc's `exit`, needing
  `_cleanup`, came in next to uClibc's), which also means an archive must not be given
  twice and must not hold a member twice (busybox's libunarchive.a does: the archives are
  rebuilt without duplicates).
- Tested (`user/test/uctest.c`, all pass): printf/scanf incl. `%f %e %g %lld`, strtod,
  libm (sqrt sin exp atan2 pow log fmod), malloc/realloc/free, qsort, setjmp/longjmp,
  signal + kill, alarm + sigsetjmp/siglongjmp (mask restored), vfork + execl + waitpid,
  pipe + dup2, stdio files, stat, opendir/readdir, errno/strerror, regex groups, uname,
  gettimeofday, strftime/gmtime; `retest` (regex, glob).

### busybox 1.00

- Config `user/busybox.config` (made from `user/busybox-seed.config` with busybox's
  `conf -o`), 123 applets: the 2.0 set (except sash) plus awk (with math), bunzip2/bzcat,
  gzip/gunzip/zcat, tar (gz/bz2), cpio, unzip, patch, cal, dc, expr, od, seq, sha1sum,
  strings, time, top, watch, crond/crontab, fbset, fsck.minix, freeramdisk, getopt,
  hostname, id, install, mktemp, readlink, realpath, renice, run-parts, uudecode/uuencode,
  who/whoami, msh/lash/hush; command line editing with history and tab completion;
  networking (2026-11-12): ifconfig (+status), route, ping (+fancy), netstat, nc, telnet,
  telnetd, httpd (no CGI), inetd (no builtins). Not: ash (refuses to build for uClinux),
  syslogd/klogd/logger (not tried yet), uncompress (750 KB static tables in every process's
  bss), insmod/rmmod/lsmod/modprobe (ELF modules; `/sbin/insmod` etc. are `modtool`),
  login/passwd (no need yet; telnetd no longer selects login, whose crypt() DES tables
  added 77 KB of bss to every busybox process). 131 applets; busybox.bflt: 411 KB text,
  63 KB bss (1.00 without networking: 372 KB / 57 KB).
- `/bin/sh` is hush (job control). Its 1.00 version vforks on no-MMU uClibc; fixed here:
  the child reset SIGTTOU to default before its `tcsetpgrp` (a background group's
  tcsetpgrp then stops it, and the vfork parent waits for its exec: hang at the first
  command); the child also joins the job's process group itself (the parent's setpgid
  comes after the exec and fails); SIGCHLD is SIG_DFL, not SIG_IGN (2.4 then reaps
  children itself and waitpid failed before a job ended); checkjobs waited for one
  process only (prompt before a pipeline ended), Ctrl-Z now makes a stopped job (own copy
  of the pids: the old code shared the parse tree's arrays and double-freed them), `fg`
  waits for the job, `bg`, `jobs`; single-quoted backslashes are kept (`printf '%s\n'`).
- `include/asm-st20/param.h` (kernel tree, seen by user space through uClibc's
  `include/asm`) includes `linux/config.h` only for `__KERNEL__`: with networking in the
  kernel config, autoconf.h's `#undef CONFIG_TR` (Token Ring) removed busybox's `tr`
  applet (`tr: applet not found`).
- hush expanded `$?` (like `$$`, `$!`) while parsing, so `false; echo $?` on one line
  printed the status from before the line; `$?` is now a variable looked up when the
  command runs (`lookup_param`).
- Daemons on no-MMU (fork() is vfork: the parent waits until the child execs or exits):
  telnetd and httpd do not call daemon() (it is not in uClibc without an MMU, and would
  hang): start them in the background (`telnetd -l /bin/sh &`, `httpd -p 80 &`); their
  per-connection children `_exit` (telnetd's exec the shell on a pty, httpd's serve one
  request: connections are handled one after the other). inetd re-executes itself with
  `-f` through busybox's `vfork_daemon_rexec` (1.00 passed it 3 of its 5 arguments and
  called daemon() first); each service's child dups the socket and execs.
- Other busybox changes: stty.c attribute before a definition's return type (gcc 2.7.2
  parse error), lash `read` buffer was a VLA, `user/include/stdbool.h`. (awk.c's
  `t.string = --p` was split for a cc1 abort; upstream again since the gcc fix.)
- Build: busybox's Makefile builds the objects and archives (`user/busybox-libs.mk` target
  `bb-libs`, `make -j6`, CROSS=t450-, `-O2 -fno-builtin -mt450
  -D__uClinux__`); a changed config rebuilds everything (objects do not depend on it
  reliably). BB_VER/BB_BT come from `conf`'s environment.
- Boot: busybox init with `rootfs/etc/inittab` (sysinit `/bin/sh /etc/rc` and a respawning
  `-/bin/sh` on ttyS0, both on the vfork path of init, which works because init waits for
  sysinit anyway); `/etc/passwd`, `/etc/group` (root) for ls/ps names.
- Tested in st20emu (`tools/userspace.script`, `tools/smoke.script`): init -> rc -> hush
  prompt; files, hard and symbolic links, cp/mv/rm, pipes (`seq | sort -r | head`, `tr |
  sort | uniq -c`), sed groups, grep -E/-i/-c, awk with floats and sqrt, printf `%f %e`,
  for/while/if/test/&&/||, `$(...)` and backquotes, glob, /proc (self/status, uptime,
  loadavg, meminfo, interrupts, mtd), dmesg, mkfs.minix + mount/umount of /dev/ram1, tar +
  gzip + gunzip | tar t, find, xargs md5sum, od, hexdump, date, cal, time, watch, ps,
  killall, `cmd &` + jobs, Ctrl-Z + jobs + bg/fg, Ctrl-C of a foreground job ($? = 130),
  vi (insert, `:wq`), sigtest (handler, SIGILL), fbtest/evtest (uClibc builds), JFFS2
  `/etc/config`.

### Not done / known problems

- hush 1.00 is upstream's "testing" shell: command substitution happens at parse time, so
  `n=$(expr $n + 1)` inside a loop body sees the value from before the loop; no
  `$((...))`, no functions, no `trap`/`break`/`continue`; in a `for` body `"$f "` loses
  the trailing space; a bg job's sleep that was stopped and continued ends with EINTR
  ("Interrupted system call", as 2.4 nanosleep does); a double prompt after ^C/^Z.
  msh (`/bin/msh`) runs scripts with functions-less Bourne syntax but has no `$(...)`.
- No `hexdump -C`, `top -n`, `head -3` in busybox 1.00 (use `hexdump -c`, `head -n 3`).
- vfork returned -errno on failure until 2026-11-12 (frameless stub). The stub with its own
  4-word frame (`vfork.ast` above) failed before because of a kernel gap: busybox init's
  sysinit child (itself a vfork child running on its kmalloc'd stack copy) vforks the
  command, and `copy_vfork_stack` measured that copy up to the parent's `env_end`,
  > 64 KB away, so the grandchild shared the child's stack and overwrote its caller's
  return address (the frameless stub survived only because the kernel restores the
  syscall frame's word 0). `copy_vfork_stack` now copies such a stack up to the end of
  its own copy (`thread.vfork_stack_end`). `uctest vforkfail` (non-root uid,
  RLIMIT_NPROC 0) checks `vfork() == -1 && errno == EAGAIN` (`tools/userspace.script`).
- hush 1.00 (seen 2026-11-12, also with the previous image): `echo g*` with a relative
  pattern in the current directory prints `g*` (absolute patterns like `/etc/*` work);
  `(echo a; echo b) | wc -l` prints 2 and then the login shell exits (init respawns it);
  `for i in 1 2; do echo $i > f$i; done` writes to the terminal, not the files.
- Not built: linuxthreads (libpthread), shared libraries, ldso, LFS, locale.

## gcc-2.7.2-t800 bugs found

All of these are fixed in `../2.0`'s gcc since 2026-11-11/12 (`../2.0/patches/gcc-2.7.2-st20.diff`,
end of section "Core kernel features"; `../2.0/README.md` "Toolchain fixes"); the kernel and
the user space are built without the workarounds described here since 2026-11-12, the
reproducers stay in `logs/`. The alloca, designated-initialiser and igmp.c (section
"Networking") bugs and ttools ast's long-string overflow are fixed too; left: calls before
the first alloca in a loop (warning, below).

cc1 dies with SIGSEGV in loop strength reduction (`-O2`) on `fs/proc/array.c`
(`task_name`) and `fs/block_dev.c`. Reduced reproducer:
`logs/gcc-2.7.2-t800-strength-reduce-crash.c` (a `do { ... continue; ... } while (i)`
copy loop):

```c
char *task_name(char *name, char *buf)
{
	int i = 16;
	do {
		unsigned char c = *name;
		name++; i--;
		*buf = c;
		if (!c) break;
		if (c == '\\') { buf[1] = c; buf += 2; continue; }
		buf++;
	} while (i);
	return buf;
}
```

`t450-gcc -O2 -S` crashed; `-fno-strength-reduce` or `-O1` compiled it. The kernel and
uClibc/busybox were built with `-fno-strength-reduce` until the gcc fix.
`logs/cc1test.sh` (try cc1 options on a kernel file) and `logs/reduce.py` (ddmin over a
`.i` file in a running container) were used to find and reduce it.

Found building the user space (reproducers in `logs/`; `logs/reduce-lines.py` reduces a
`.i` by lines while cc1 still aborts at -O2 and the file still compiles at -O0):

- `gcc-2.7.2-t800-alloca.c`: `__builtin_alloca` (and VLAs, and calls of any function named
  `alloca`) switch to a new workspace below the block for later calls and save the old
  Wptr in its word 3, which is the block's first word: data written there is lost, and a
  call that writes the block corrupts the saved Wptr. `al(100)` returns 5659, not 4950.
  Fixed for direct and pointer calls in gcc 2026-11-11 (block above the save slot). Then
  `gcc-2.7.2-t800-alloca-gcall.c`: a call from an inlined function ran on the old Wptr (no
  `gajw` before the inlined syscall's `gcall`). Root cause: t800_expand_call decides at RTL
  expansion, per call, whether to retarget Wreg (`current_function_calls_alloca`); the
  inlined function's calls had been expanded in its own context and were copied verbatim.
  Library calls had the same problem (emit_library_call never sees FUNCTION_ARG_PRESCAN,
  so `__divdi3`/soft float used `call` with register arguments on the normal Wreg).
  Fixed 2026-11-12 (`../2.0/x-softfloat/sr/gcc-alloca-gcall.diff`, test `sr/rt/alloca2.c`):
  no inlining of functions that make calls after an alloca, libcalls pass everything on
  the stack in alloca functions, and gcc-2.7.2's emit_library_call stored stack arguments
  at the wrong offsets (`argvec[count]` for `argvec[argnum]`). Not fixable at expansion
  time: a call before the first alloca in a loop containing it (warning; uClibc glob.c).
- `gcc-2.7.2-t800-double-poly-abort.c`: cc1 aborts (reg-stack, SIGABRT) at -O1/-O2 on a
  nested double polynomial of array loads followed by an ordered integer compare (uClibc
  libm `k_tan.c`, `s_atan.c`); no -f option avoids it; compare first.
- `gcc-2.7.2-t800-predec-abort.c`: cc1 aborts at -O1/-O2 on `t.string = --p` with `p`
  addressable (busybox awk `next_token`); `p--; t.string = p;` compiles.

## Known issues / not done

- Integer division by zero is not trapped (as in 2.0: the trap would also fire on add
  overflow, which gcc-t800 emits for wrapping arithmetic).
- System call restarting, ptrace, SysV IPC, rt signal frames, six-argument calls and
  freeing init memory: done (section "Core kernel features"); networking (loopback, SLIP)
  and modules: done (section "Networking and loadable modules", with its own "Left to do").
- The kernel image is 721,776 bytes of the 768 KB kernel partition (64 KB free; romfs: 633
  of 704 KB).
- st20emu gaps (emulator not changed): no flash chip model for `--board dsr9500` (MTD runs
  on `map_ram` there; CFI/JEDEC untested); the flash is saved only by an explicit `dump`
  (no save on exit); the OSD renderer reads a 24-bit (Tc) palette entry's Cb from bits
  7:0 and Cr from bits 15:8, the datasheet (table 93) has them the other way round (st20fb
  uses 14-bit palettes, unaffected); 4-bit chroma is expanded as `c << 4 | c` (8 -> 0x88),
  which tints neutral greys slightly purple in screenshots; at most 256 blocks per field
  are drawn (st20fb uses 241).
- User space: see "User space, not done / known problems".

## Core kernel features (merged 2026-11-11 from work/core)

Changes in `core.diff` (against the 2.4 kernel tree as of this date). Everything below is
in `arch/st20`, `include/asm-st20`, plus `include/linux/init.h`, `mmnommu/mmap.c`, `.config`.

### 1. System call restarting

- `st20_syscall_c` (syscalls.c) passes the call's result and its workspace `uw` to
  `st20_handle_signals(u, sc)` (signal.c). i386 rules: `-ERESTARTNOHAND`, `-ERESTARTSYS`,
  `-ERESTARTNOINTR` are made again when no handler runs (stop + continue, ignored or
  default-ignored signals, a tracer that suppresses the signal); with a handler,
  `-ERESTARTNOINTR` always restarts, `-ERESTARTSYS` only with `SA_RESTART`, everything else
  becomes `-EINTR`. With no signal pending at all, an `-ERESTART*` becomes `-EINTR`.
  2.4 has no `-ERESTART_RESTARTBLOCK` (nanosleep returns `-EINTR` with the remaining time,
  as on 2.4 i386).
- Re-executing: `uw[1..7]` still hold the call (nr, a..f), so the user state continues at
  `st20_syscall_restart` (entry.ast, kernel text run in user mode) with Wptr = `uw`. It
  is `ldc 0xc0000000; ldnl 0; gcall`: it enters the vector exactly like the original call
  did (like the vfork stub), and the restarted call returns to `uw[0]`, the original
  return address. With a handler, the handler frame goes below `uw` and the handler's
  return (sigreturn) continues at the restart stub.
- The kernel's own `_syscallN` calls (`st20_kernel_syscall`) still turn `-ERESTART*`
  into `-EINTR`.

### 2. rt signals with siginfo

- `SA_SIGINFO` handlers (from `rt_sigaction` or uC-libc's old `sigaction` 67, which
  passes the flag) get `handler(sig, siginfo_t *, struct ucontext *)` (Areg-Creg and the
  frame's argument words 1..3). The frame below the interrupted Wptr is the 4-word call
  frame (return address `st20_rt_sigtramp`, sig, &info, &uc) + `struct st20_rt_sigframe`
  { siginfo, ucontext }. `include/asm-st20/ucontext.h`: `uc_mcontext` is `struct
  sigcontext` (sc_mask = first mask word, sc_state = Wptr, Iptr, Areg, Breg, Creg),
  `uc_sigmask` the full old mask. `st20_rt_sigtramp` calls `rt_sigreturn` (173), which
  restores `uc_sigmask` and `uc_mcontext.sc_state` (a handler may change both). Handlers
  without `SA_SIGINFO` keep the old frame and sigreturn (119).
- `copy_siginfo_to_user` drops the kernel-internal `__SI_*` class bits of `si_code`
  (as i386), so user space sees `SI_USER` 0, `SI_QUEUE` -1, `FPE_INTDIV` 1 ...
- `SA_ONSTACK`: frames go on the `sigaltstack` (`frame_base`).
- rt_sigprocmask / rt_sigpending / rt_sigtimedwait / rt_sigqueueinfo / kill info are the
  generic 2.4 ones (already in the table); rt_sigsuspend delivers with the waiting mask as
  before.

### 3. Six system call arguments (mmap2, ipc)

- Convention: user code calls the vector `*(long (**)())0xc0000000` with
  `(nr, a, b, c, d, e)` as before, and for the 6-argument calls with a 7th word:
  `(nr, a, b, c, d, e, f)`. gcc-t800's call puts nr, a, b in Areg-Creg (`call` stores them
  in uw[1..3]) and c, d, e, f are the caller's outgoing argument words uw[4..7].
  The kernel reads uw[7] only for `mmap2` and for the two `ipc` calls that use their
  6th argument (SEMTIMEDOP, MSGRCV version 1) (`six_args()` in syscalls.c): all other
  callers, including uC-libc's and uClibc's 5-argument stubs (`ipc()` too) and the vfork
  stub, stay valid (uw[7] there is some word of the caller's frame, maybe never written).
  `include/asm-st20/unistd.h` has `__st20_syscall6` / `_syscall6` (user side); with it
  uClibc 0.9.26's mmap64 (libc/sysdeps/linux/common/mmap64.c) uses mmap2.
  `struct pt_regs` has `arg[6]`; the table entries are called with 6 arguments.
- `mmnommu/mmap.c`: the copy path of a file mapping read from `file->f_pos` (and moved it)
  instead of from the mapping offset; it now reads at `pgoff << PAGE_SHIFT`. Before this,
  mmap's offset was ignored for every file without its own `mmap` (romfs, minix, JFFS2).
- uC-libc's `ipc()` (5 words) still works: `fifth` is 0 for it.
- Test: mmap2 at pgoff 1 and 3 maps the right file bytes, MSGRCV version 1 with msgtyp
  as 6th word picks the type-9 message, SEMTIMEDOP's timeout (6th word) expires.

### 4. Integer division by zero -> SIGFPE

- On the ST20-C2, `div`, `rem` (by zero, and MostNeg / -1) and `ldiv` (Creg >= Areg)
  signal **IntegerOverflow**, the same error as overflowing `add`, `adc`, `sub`, `mul`,
  `ladd`, `lsub`, `fmul` (manual 72-TRN-273, error signals; st20emu instructions.cpp the
  same). gcc-t800 emits add/adc/sub for wrapping C arithmetic, so the trap cannot be fatal.
- The low-priority IntegerOverflow trap (enable bit 2, error group 1, beside IntegerError
  bit 1 and IllegalOpcode) is now enabled. The trap stub (`st20_error_trap1`, entry.ast)
  saves Areg-Creg (a trap does not save them; the trapped process's values are still in
  the registers), the C handler decodes the instruction that ends at the trapped Iptr
  (Iptr is after the trapping instruction): `22 FC` div, `21 FF` rem, `21 FA` ldiv, not
  preceded by a further pfix/nfix byte (that would be a different opr). Any other
  instruction: `tret` with the evaluation stack restored, i.e. the wrapped result, exactly
  as without the trap (st20_ovf_traps counts them, `/proc/cpuinfo` "Overflow traps").
  A division in user code: `force_sig_info(SIGFPE)` with `si_code FPE_INTDIV`, `si_addr`
  = the instruction, delivered at once from the kernel stack (`st20_trap_kill_c`), so a
  handler runs (and returning from it continues after the division, Areg undefined); the
  default action kills ("division by zero at ..., killed"). In kernel code: panic. The
  handler runs with all enables off (trap descriptor word 0), so it is not preempted.
  IntegerError (range checks) and IllegalOpcode now also go through the signal path
  (SIGFPE FPE_INTOVF / SIGILL ILL_ILLOPC, catchable).
- Cost: 22 continued overflow traps from boot through the whole test script (counter in
  /proc/cpuinfo); gcc-t800 code rarely overflows `add`/`adc`.

### 5. ptrace

- `arch/st20/kernel/ptrace.c`: TRACEME, ATTACH (SIGSTOP), PEEK/POKE TEXT/DATA (direct
  SDRAM access, flash readable; POKETEXT invalidates the I-cache), PEEKUSER/POKEUSER,
  GETREGS/SETREGS, CONT, SYSCALL, KILL, DETACH; exec of a traced task stops it with
  SIGTRAP (binfmt_flat). The user area is `struct pt_regs` (`include/asm-st20/ptrace.h`):
  word 0 orig_nr, 1-6 arg[0..5], 7 result, 8 wptr, 9 iptr, 10 status, 11-13 Areg-Creg
  (PT_WORDS = 14, GETREGS copies these), then 14 start_code, 15 start_data, 16 end_data
  (read only). At a system call stop (PTRACE_SYSCALL: entry and exit, `st20_syscall_trace`)
  thread.regs is the call's pt_regs (the tracer may change the call or, at the exit stop,
  the result); at a signal stop (`ptrace_signal_stop` in signal.c) it is a pt_regs with the
  exact user state (orig_nr/result = the interrupted call if it would restart), and
  POKEUSER of wptr/iptr/Areg-Creg changes where the tracee continues.
- PTRACE_SINGLESTEP returns -EIO: the ST20-C2 has no trace flag (single-stepping exists
  only through the diagnostic controller), and the only software breakpoint, `j 0` with
  the breakpoint trap enabled, is a jump that leaves Areg-Creg undefined (st20emu: `j_`
  pops all three), while the evaluation stack is live across most instruction boundaries
  of gcc-t800 code.
- uC-libc's `ptrace()` (`../2.0/.../sysdeps/st20/ptrace.c`) is still an ENOSYS stub (the
  2.0 tree was not changed); the tests call system call 26 directly. PEEK* store the word
  at `*data` (kernel ABI, as i386), a glibc-style wrapper returns it.

### 6. SysV IPC

- `CONFIG_SYSVIPC=y` (`arch/st20/config.in`, `.config`): ipc/msg.c, sem.c, shm.c, util.c
  (~20 KB of kernel). `sys_ipc(call, first, second, third, ptr, fifth)` as i386 (incl.
  SEMTIMEDOP 4, MSGRCV version 0 with ipc_kludge and version 1 with msgtyp = fifth,
  SHMAT returning the address through `third`), `SEMTIMEDOP` added to asm-st20/ipc.h.
- Shared memory is uClinux's NO_MM shm.c: a segment is one kmalloc'd block (<= 128 KB),
  shmat returns its address (a non-zero shmaddr is EINVAL: no fixed addresses without an
  MMU), shmdt only counts the detach; segments are not detached automatically at exit
  (nattch stays up, IPC_RMID then keeps a segment until the count drops).
- `/proc/sysvipc/{msg,sem,shm}` exist.

### 7. Freeing init memory

- `__init` functions and `__initdata` objects now go into an assembler segment `init`
  that head.ast lays out between text and data (`.text .init .data .bss`,
  `init@@start = text end`, `data@@start = init end`, `___init_begin/___init_end`), and
  `free_initmem()` (arch/st20/mm/init.c) frees its whole pages: "Freeing unused kernel
  memory: 20k freed (init 20k at c0068f9c)". The freed pages are first filled with
  `2b f1` (an illegal opcode), so a stale call into init code traps (kernel panic with the
  address) instead of running garbage. The boot banner shows the INIT range.
- How, without named sections (gcc-t800 has no `ASM_OUTPUT_SECTION_NAME`, lit none): the
  kernel's CC is `arch/st20/tools/st20-cc` (arch/st20/Makefile), a wrapper around
  t450-gcc for `-c FILE.c`: (1) a preprocessing pass with `-D__ST20_INIT_MARKS`, for which
  `include/linux/init.h` defines `__init`/`__initdata` as marker words, finds the names
  declared with them (function: identifier before the first `(` after the marker; data:
  `name[..] MARK` or `MARK type name =`); (2) the normal compile to assembly (markers
  empty); (3) `arch/st20/tools/st20-initseg` (perl) moves each such function (label in
  .text up to the next non-local column-0 line) and object (label in .data, with its
  `.align`) into `.init`, restoring the segment after it. `.globl` lines inside a piece
  stay in it; a segment switch inside a data initialiser (gcc emits string constants into
  .text mid-object) is followed and the object resumed in `.init`; a trailing `.align`
  stays with the next object; string constants (`LC@n`, possibly shared) stay in .text;
  (4) ast. Cross-segment
  references (calls, `ldc sym-L; ldpi`) are resolved by lit like text-to-data ones, and the
  `init` fragments take part in lit's dead stripping as before. `ST20_NO_INITSEG=1`
  compiles normally. The error trap handler treats text + init as kernel code.
- Gain: the init segment is 20.7 KB (21192 bytes; this lean configuration: init/main.c, do_mounts.c,
  bootmem, page_alloc, numa, setup/osd/st20fb boot code, ...); 20 KB (5 pages) is freed,
  the rest shares pages with text/data (page-aligning the segment would free 24 KB but
  cost up to 8 KB of padding). initcall/`__setup` table entries stay in .data (they are
  not __initdata on ST20). As on other archs, a function declared `__init` in a header
  is moved even if its definition omits it. Build time ~ +30% (the extra -E pass); the
  container's perl 5.10 runs st20-initseg.

### Tests (st20emu, `work/core/test`, programs against the 2.0 uC-libc)

`work/core/test/core.script` (`coretest`, `minitrace`; built by `test/build.sh`, put into
the romfs by `mkimage-core.sh`), all PASSED:

- `coretest restart`: SIGALRM handler with SA_RESTART during a blocking pipe read: the
  read restarts and returns the byte the handler wrote; without SA_RESTART: -1 EINTR;
  pause() with an SA_RESTART handler: EINTR (ERESTARTNOHAND); waitpid restarted.
- `coretest rt`: SA_SIGINFO handler via rt_sigaction: kill gives si_signo 10, si_code 0,
  si_pid = self, valid ucontext; mask in handler, uc_sigmask; rt_sigqueueinfo si_code -1
  si_value 1234; rt_sigprocmask + rt_sigpending; rt_sigtimedwait (signal, then -EAGAIN
  timeout); rt_sigsuspend (-EINTR, handler, mask restored); SA_ONSTACK on a sigaltstack.
- `coretest mmap`: mmap2 with pgoff 0, 1, 3 through the 7-word call returns the bytes at
  file offsets 0, 4096, 12288 of /bin/busybox (romfs).
- `coretest div`: 0x7fffffff + 1 wraps (overflow trap continued), a hash/sum loop gives the
  host's values; 7/0, 7%0, unsigned 9/0, 9%0 each raise SIGFPE (FPE_INTDIV, si_addr);
  7/2 + 9/3 no signal. `coretest divkill`: "division by zero at ..., killed", shell
  "Floating exception".
- `coretest ipc`: msgget/msgsnd/msgrcv (version 0, and version 1 with msgtyp as 6th
  argument), semget/semop/GETVAL, a child blocked on semop(-2) until the parent's +1,
  SEMTIMEDOP timeout (-EAGAIN), shmget/shmat/shared write by a child/shmdt/IPC_RMID,
  shmat(bad id) EINVAL.
- `minitrace -t`: exec stop (SIGTRAP), GETREGS + PEEKTEXT at the entry, SINGLESTEP -EIO,
  KILL, POKEUSER of getpid's result (child exits with 4242 & 0xff), ATTACH to a spinning
  program + PEEKDATA/POKEDATA of its flag + DETACH (it exits with the poked value), ATTACH
  during a blocking read (stop shows orig_nr 3, result -512 = -ERESTARTSYS) + DETACH -> the
  read restarts without a handler and gets the byte written later.
- `minitrace /bin/sigtest raise`: strace-like listing (sigaction, getpid, kill,
  `--- signal 10 ---`, the handler's write, sigreturn, exit).
- `tools/smoke.script` (paths to work/core/logs): unchanged output (busybox md5sum, pipes,
  signals, SIGILL, Ctrl-C loop, panel, IR, MTD/JFFS2, fb, evtest).

### Applying core.diff to the main tree

```sh
# from uClinux/2.4 (git apply keeps the scripts' 755 mode)
git -C src/uclinux-dist apply --directory=linux-2.4.x ../../work/core/core.diff
# or: cd src/uclinux-dist/linux-2.4.x && patch -p1 < ../../../work/core/core.diff &&
#     chmod +x arch/st20/tools/st20-cc arch/st20/tools/st20-initseg && cd ../../..
./dbuild.sh /work/kbuild.sh oldconfig dep                   # CONFIG_SYSVIPC, new ptrace.c
./dbuild.sh /work/kbuild.sh clean                           # CC changed: rebuild everything
./mkimage.sh -j6 && tools/mkpatches.sh                      # patches/ incl. the .config
```

Checked: the diff applies with `patch -p1 --dry-run` to `src/uclinux-dist/linux-2.4.x` as of
2026-11-11 22:46 (none of the touched files differed from the work/core baseline); applied
to a clean copy of the baseline it builds (oldconfig, dep, clean, `make linux.bin`,
613928 bytes, init segment 21204 bytes) and passes `core.script`. The test programs are
not in the main tree: `work/core/test/{coretest,minitrace}.c` can move to `tools/test`
(add them to `tools/test/build.sh`'s list and to mkimage.sh's romfs), `core.script` to
`tools/`. `work/core/mkimage-core.sh` (`NOKERNEL=1`/`NOTESTS=1`) builds
`work/core/dsr9500-core.bin` with them.

Strength reduction is enabled again: the cc1 crash was a reg-stack bug (REG_NONNEG note
without a value), fixed in ../2.0 gcc together with reload1.c (live argument registers chosen
as reload registers), local-alloc.c (REG_EQUIV to MEM with a register-plus-offset address)
and alloca (block overlapped the Wreg save slot). Fix: ../2.0/patches/gcc-2.7.2-st20.diff.

## Networking and loadable modules (merged 2026-11-12 from work/net)

`work/net/net.diff` (kernel; applied with `git -C src/uclinux-dist apply
--directory=linux-2.4.x ../../work/net/net.diff` -- from inside `linux-2.4.x` a plain
`git apply` silently skips every path, they are outside the current directory), the module
tool chain (now `modules/`), the test programs (now `user/net/`, built with uClibc by
`user/build-user.sh`) and the SLIP host (now `tools/slip_host.py`).  `mkimage.sh` builds the
modules after the kernel and puts them into `/lib/modules`.

### Kernel size

| | bytes | of the kernel partition |
| --- | --- | --- |
| before networking (2026-11-11 23:24) | 615,456 | 85 % of 704 KB |
| with net.diff: text 652,352 + init 26,536 + data 35,324 (bss 142,928 not in the image) | **714,212** | 90.8 % of 768 KB (99.1 % of the old 704 KB) |
| 2026-11-12 toolchain round: + `CONFIG_MTD_JEDECPROBE`, fixed gcc, vfork stack fix | **721,776** | 91.8 % of 768 KB (64 KB free) |

The kernel/romfs boundary moved by 64 KB for it (kernel 768 KB, romfs 704 KB, section "Flash
image").  Without trimming the networking kernel was 873,836 bytes (text 689 KB + data
99 KB + bss 85 KB).  What made it fit:

- **bss is no longer in `linux.bin`** (`arch/st20/Makefile`: crossload's trailing bss zeros
  are cut off with the sizes from `linux.map`; `head.ast` clears bss before `start_kernel`).
- **no `-fno-common`**: with it gcc-t800 emits every uninitialised global as zeros in
  `.data`; as `.comm` lit puts them into bss (60 KB of zeros left the image).  Duplicate
  tentative definitions now merge like in any C build; a `.comm` plus an initialised
  definition is still a link error in lit.
- `CONFIG_ZLIB_DEFLATE` off: JFFS2 (`fs/jffs2/compr_zlib.c`) no longer compresses with
  zlib (rtime/rubin or uncompressed nodes) but still reads zlib nodes; saves 16 KB and the
  ~270 KB deflate workspace vmalloc at boot.
- `CONFIG_SLIP_COMPRESSED` off (no VJ header compression, slhc 4.5 KB).
- IGMP stubs (`net/ipv4/igmp.c`, ST20 without `CONFIG_IP_MULTICAST`, 5.7 KB): no multicast
  groups (not even 224.0.0.1), multicast socket options fail; `/proc/net/igmp` empty.
- `CONFIG_MTD_JEDECPROBE` was off (its table was miscompiled, below); on again since the
  gcc fix (2026-11-12, +7.5 KB).
- `arch/st20/kernel/ksyms.c` lists only functions the kernel has anyway.

PPP (ppp_generic + ppp_async + slhc, about 45 KB linked, estimated) would fit into the 64 KB
now free, but is not built. The full igmp.c (multicast) compiles at -O2 since the gcc fix
and would add ~6 KB (tested: 720,272 bytes without JEDEC).

### Networking

Config (`arch/st20/config.in`, all `define_bool`): `CONFIG_NET`, `CONFIG_INET` (IPv4, TCP,
UDP, ICMP, raw sockets), `CONFIG_UNIX`, loopback (always built), `CONFIG_NETDEVICES`,
`CONFIG_SLIP`, BSD ptys (majors 2/3, `/dev/ptyp0-3`, `/dev/ttyp0-3`); no packet sockets,
netfilter, multicast, IPv6, PPP, Ethernet drivers.

Socket calls go through `socketcall` (102) with an argument block: uClibc 0.9.26 uses
`__NR_socketcall` when the arch has no separate socket calls (asm-st20/unistd.h has none),
like the 2.0 uC-libc; the system call table already had `sys_socketcall` at 102.

Fixes needed on the way (all in the kernel patch):

1. **ttools `ast` heap overflow on long strings** (`ast/scan.l` `mk_string_space` grows
   its buffer once per token: a run of more than ~80 plain characters in one `.ascii`
   overflows; `net/ipv4/proc.c`'s 989-character TcpExt header aborted in realloc, shorter
   overflows may corrupt silently).  Worked around first by `as` wrappers splitting long
   `.ascii` lines (kernel `-B arch/st20/tools/`, `user/bin/as`); fixed in ast since
   2026-11-12 (`../2.0/patches/ttools-2.0beta2.diff`), the wrappers are gone and
   `/proc/net/netstat`'s 968-byte header line comes out whole.
2. **gcc-2.7.2-t800 designated-initialiser bug**: in an initialiser that has a nested `{ }`
   member, the first gap (skipped member) before it gets a bogus extra gap of the nested
   member's offset, so every later value lands that far too late.  `arp_tbl` (neighbour
   table) came out zero -> `kmem_cache_create(NULL, 0)` BUG at boot.  Worked around by
   naming all members before `parms` in `net/ipv4/arp.c`; `drivers/mtd/chips/jedec_probe.c`'s
   `jedec_table` had it too (a bogus 24-byte gap per entry), so JEDEC probing was off.
   Fixed in gcc's c-typeck.c since 2026-11-12 (generic gcc-2.7.2 push_init_level bug,
   `../2.0/x-softfloat/tc/NOTES.txt`): arp.c is upstream's again, JEDEC probing is on
   (still untested on hardware: st20emu has no flash model for this board).  Reproducer:
   `struct p {void *n; int x;}; struct s {void *next; int family; struct p parms;};
   struct s b = { family: 2, parms: { x: 5 } };` gives `.byte 0[8]; .byte 0[4]; .word 2 ...`.
   gcc also aborted (cc1 signal 6) on `net/ipv4/igmp.c` at -O2 (worked around with
   `CFLAGS_igmp.o := -fno-expensive-optimizations`, removed 2026-11-12): already fixed by
   the local-alloc.c REG_EQUIV change of 2026-11-11.
3. **checksum byte order**: `arch/st20/lib/checksum.c` (from big-endian m68knommu) added an
   odd trailing byte as the high byte; ST20 is little endian.  Every odd-length packet
   (e.g. a 47-byte ping) failed its checksum.
4. **loopback without Ethernet header**: gcc-t800 aligns every struct to 4 bytes and
   accesses the 2-byte aligned `ethhdr` in front of the IP header with whole-word
   loads/stores (`eth->h_proto` read-modify-write); st20emu tolerates that with a warning,
   a real ST20 does not.  `drivers/net/loopback.c` (ST20 only) has no hardware header (like
   SLIP) and sets `protocol = ETH_P_IP` itself.  Any future Ethernet driver has the same
   problem (would need packed accessors in eth.c).
5. **SLIP write_wakeup**: SLIP stops the device queue after each frame until the tty driver
   calls `write_wakeup`; the ST20 serial driver transmits polled and never did, so every
   second packet waited for the 20 s tx watchdog.  `arch/st20/kernel/serial.c` now
   schedules a tasklet calling `ldisc.write_wakeup` when `TTY_DO_WRITE_WAKEUP` is set, and
   does not mirror non-N_TTY output (SLIP frames) to the TV console.

User space: busybox `ifconfig`, `route`, `ping`, `netstat`, `nc`, `telnet`, `telnetd`,
`httpd`, `inetd` (section "busybox 1.00"); `/etc/hosts`, `/etc/services`, `/etc/protocols`,
`/etc/inetd.conf` (echo -> cat, daytime -> date); `/etc/rc` runs `ifconfig lo 127.0.0.1 up`
and `hostname dsr9500`.  `nettest` (`user/net/nettest.c`) does what busybox 1.00 cannot:

```
nettest lo                         loopback self test: UNIX socketpair, UDP, TCP, ICMP ping
nettest ping ADDR [COUNT]          ICMP echo through a raw socket
nettest ifconfig IF ADDR [PEER]    set address (and point-to-point peer), bring IF up
nettest route add GW               default route via GW
nettest echod PORT                 TCP echo server (one client at a time, forever)
nettest slip TTY ADDR PEER PORT    SLIP on TTY (N_SLIP line discipline = slattach), configure
                                   the interface, ping PEER until it answers, then echod PORT
```

There is no slattach in busybox 1.00; the SLIP mode does everything in one process because
ttyS0 is also the console (once its line discipline is N_SLIP nothing more can be typed;
nettest sets the console log level to 1, printk would go to the line too).

SLIP test against the host: st20emu `--serial-tcp 3:5555` (ASC3 = ttyS0 on a TCP port)
and `tools/slip_host.py` (a user-space IPv4/ICMP/TCP end of the link: waits for the port
and the target's first ping, pings back, then talks to the target's TCP echo server):

```sh
../../builddir-release/st20emu --soc sti5512 --board dsr9500 --firmware dsr9500-uclinux24.bin \
    --serial-tcp 3:5555 --script tools/slip.script > logs/slip-emu.log 2>&1 &
python3 tools/slip_host.py --port 5555          # RESULT: all ok
```

### Loadable modules

lit has no relocatable output and 2.4's insmod wants ELF, so modules are **linked on the
host against the exact running kernel** and relocated by a small loader:

- Build (`modules/build.sh NAME...` in the container, run by `mkimage.sh` for every
  `modules/*.c`): a module is compiled like a kernel object (kernel CFLAGS,
  `KBUILD_BASENAME`, not `-DMODULE`). `module_init(fn)` is the ST20 `__initcall` (global
  `__st20_ic__BASE__LINE__fn`), `module_exit(fn)` defines `__st20_ec__BASE__LINE__fn`
  (`include/linux/init.h`; in the kernel nothing refers to it, lit drops it with the exit
  function, so built-in drivers cost nothing).
- Link (`modules/modlink.sh`, container): every symbol the objects use that `linux.map`
  has becomes an absolute ast assignment in a stub (which also starts the segments and
  refers to the init/exit variables), libgcc fills the rest; lit links the module twice, at
  0x50000000 and 0x51000000 (2 GB from the kernel: every kernel reference gets the full
  8-byte pfix/nfix sequence in both links).
- `modules/mkmod.py` (host) diffs the two images: aligned words differing by the base
  delta are absolute module pointers (ABS relocs), pfix/nfix sequences whose operand
  differs by -delta are PC-relative references to the kernel (`call`, `ldc sym-L; ldpi`)
  (PCREL relocs, +delta: absolute module addresses in code). Anything else is an error.
  Header: magic `S20M`, version, FNV-1a hash of the kernel's `linux_banner` (build number +
  time of the exact kernel), link base, sizes, init/exit offsets, name.  Output
  `build/mod/NAME.mod` -> `/lib/modules/NAME.mod`.
- Kernel (`arch/st20/kernel/module.c`, `CONFIG_ST20_MODULES`): system calls 128
  `init_module(image, len)` and 129 `delete_module(name)` (replacing 2.4's struct-module
  calls, which were ENOSYS without CONFIG_MODULES); kmalloc(image + bss), copy, clear bss,
  apply ABS (+= load - base) and PCREL (operand -= load - base, re-encoded in the same
  number of bytes, padded with leading `pfix 0`), I-cache invalidate, call init (nonzero
  = unload again); rmmod calls exit and frees.  `/proc/modules` lists them.  No use counts:
  rmmod with open files is the user's problem.  `arch/st20/kernel/ksyms.c` keeps ~70
  functions modules commonly need (chrdev, proc, timers, wait queues, irq, kmalloc, netdev,
  skb, slab, string/printf) in the kernel, since lit drops everything unreferenced;
  anything else the kernel uses is available anyway.  Cost: about 3 KB.
- User space: `user/net/modtool.c` = `insmod FILE.mod`, `rmmod NAME`, `lsmod` (links in
  `/sbin`); busybox's insmod/rmmod/lsmod are not built (ELF, query_module).

Sample `modules/hello.c`: char device `/dev/hello` (major 61; read = greeting with open count
and last message, write = store message) and `/proc/hello`.

Limits: a module must be relinked for every kernel build (the banner hash refuses a stale
one with ENOEXEC; `mkimage.sh` does it); kmalloc limits a module to 128 KB; no module
parameters, no inter-module symbols, no `MOD_INC_USE_COUNT` (`THIS_MODULE` is NULL).

### Tests (st20emu, `tools/net.script`, `tools/slip.script`)

All with the image from `./mkimage.sh -j6` (kernel 714,212 bytes, romfs 661,504 bytes),
2026-11-12; `tools/smoke.script` and `tools/userspace.script` pass unchanged (uctest
PASSED with the new alloca check, awk with sqrt, tar/gzip, jobs, vi, MTD/JFFS2, fb).
Again after the toolchain round (clean kernel + user space, kernel 721,776 bytes, romfs
648,192 bytes; `logs/test-*.log`, `logs/slip-host.log`): smoke, userspace (uctest
PASSED, `uctest vforkfail` ok), net and SLIP (`RESULT: all ok`) give the same output as
before apart from addresses, timings and the busybox md5sum.

`tools/net.script` (`logs/test-net.log`):

```
ifconfig                          lo  inet addr:127.0.0.1  Mask:255.0.0.0  UP LOOPBACK RUNNING
ping -c 3 127.0.0.1               3 packets transmitted, 3 packets received, 0% packet loss
ping -c 1 -s 47 localhost         55 bytes from 127.0.0.1 (odd length: checksum fix)
nettest lo                        loopback: all ok (UNIX, UDP, TCP 20000 bytes, raw ICMP)
nc -l -p 5000 & ... | nc ...      through nc;   netstat -an: TIME_WAIT entries
false; echo rc=$?                 rc=1 (hush $? fix); nettest nosuchmode -> rc=2
insmod /lib/modules/hello.mod     init_module: hello at c06e0800 (948 bytes); lsmod; 61 hello
cat /dev/hello; echo .. >; cat /proc/hello   greeting, then message "written by echo"
insmod again / rmmod / rmmod      File exists rc=1 / module unloaded rc=0 / No such file rc=1
inetd; nc 127.0.0.1 7; nc .. 13   "inetd echo" (cat), date (daytime); ps: inetd -f
httpd -p 8080 &; GET via nc       <h1>ST20 httpd</h1>
telnetd -l /bin/sh -p 2323 &      netstat: LISTEN on 7, 13, 8080, 2323
telnet 127.0.0.1 2323             hush banner; tty -> /dev/ttyp0; exit -> Connection closed
```

SLIP (`tools/slip.script` with `--serial-tcp 3:5555`, `tools/slip_host.py`,
`logs/slip-host.log`): three pings answered (3-8 ms), TCP echo on port 7 ok,
`RESULT: all ok`.

### Left to do

- PPP not built; IGMP/multicast stubbed (size, not the compiler).
- Toolchain bugs found here are fixed in `../2.0` (ast strings, designated initialisers,
  igmp.c abort, alloca); upstream ttools/gcc-t800 are unmaintained.
- Ethernet header accesses are word-sized on unaligned headers (a real Ethernet driver
  would need eth.c fixes); only loopback (headerless) and SLIP are done.
- SLIP shares ttyS0 with the console; a second ASC for SLIP would need a second serial
  port in serial.c.
- Modules: no use counts, parameters or inter-module symbols; relink per kernel build.
- Tested only in st20emu (SLIP over `--serial-tcp`, a user-space host stack; no real
  slattach/PPP peer).
