# uClinux on ST20 (st20emu)

Goal: a native uClinux port (`arch/st20`) for the **Samsung DSR9500** (STi5512), the only
supported board, tested on st20emu (this repo, `..`). No ST20 Linux port has existed before.

## Toolchain (done, 2026-11-11)

| piece | source | notes |
| --- | --- | --- |
| C compiler | gcc-2.7.2 + gcc-t800 patch v12 (Yury Shevchuk, 1997), target `t450` | `tools/bin/t450-gcc`, GNU C incl. inline asm |
| assembler | ttools-2.0beta2 `ast` | `tools/t450/bin/as` |
| linker | ttools `lit` (LIT object format, fragment-level dead stripping, `-T addr`, `-m` map, entry hook `@@hook`) | no `ld -r` |
| image | `crossload` (from ttools startups/absolute) | LIT executable -> raw image |

### Layout in git

Only the port's own files are tracked. `dl/` (downloads), `src/` (third-party trees) and the
built `tools/` are ignored; every change to third-party code is kept in `patches/`:

| patch | applies to |
| --- | --- |
| `patches/gcc-2.7.2-st20.diff` | gcc-2.7.2 + t800 patch v12 |
| `patches/ttools-2.0beta2.diff` | ttools-2.0beta2 |
| `patches/linux-2.0.x-st20.diff` | uClinux-dist-20040218 `linux-2.0.x` (incl. new `arch/st20`, `include/asm-st20`) |
| `patches/linux-2.0.x.config` | kernel `.config` |

Edit in `src/`, then run `tools/mkpatches.sh` before committing.

### Fresh checkout (all commands from `uClinux/2.0/`)

```sh
tools/setup-src.sh                                   # downloads + patches -> dl/, src/
docker build --platform linux/386 -t st20-build docker
docker run --rm --platform linux/386 -v $PWD:/work st20-build sh /work/build-toolchain.sh
```

The image `st20-build` is i386 Debian Lenny with gcc 4.3; everything runs under qemu on
Apple Silicon (a clean kernel build takes ~6 min).

Source fixes for a modern host (in `patches/`): `-m486`, errno/`fpos_t` in ttools,
multi-line string in lit options, gcc `config.sub` t450, `genoutput.c` escapes newlines in
templates, t800.h/t800.md `abort()` casts, reload1.c label; only `xgcc cc1 cpp libgcc.a specs`
are built (c++filt fails, unneeded).

ABI (generic run-time model): first 3 argument words in Areg/Breg/Creg, callee reads them as
`ldl 1..3` after `call`; result in Areg (64-bit in A/B); globals addressed PC-relative
(`ldc sym-L; ldpi`), no static link, so code+data are position independent except
pointer initialisers. Inline asm constraints `a b c` (A/B/C), `t u v` (FP), `P` = popped.

Test: `test/crt0.s` + `test/hello.c` -> `test/hello.bin` (linked at 0xc0000000):

```sh
docker run --rm --platform linux/386 -v $PWD:/work -w /work/test st20-build sh -c \
 'export PATH=/work/tools/bin:$PATH; t450-gcc -O2 -c hello.c && ast -o crt0.o crt0.s &&
  lit -T 0xc0000000 crt0.o hello.o -o hello.lit && crossload -o hello.bin hello.lit'
../../builddir-release/st20emu --soc sti5518 --firmware test/hello.bin --address c0000000 --run 20000
# ASC0> Hello from gcc-t450 on ST20
```

## Kernel

Base: uClinux 2.0.39-uc2 (`src/uclinux-dist/linux-2.0.x`, sparse clone of
github.com/neozeed/uClinux-dist-20040218). Kernel 2.0 was built with gcc 2.7.2 and has no
initcall/section magic, which suits `lit`. Template arch: `h8300` (~7.8k lines), also `i960`,
`shnommu`.

### Design (ST20-C2 specifics)

- Everything Linux runs (kernel and user tasks) is ONE low-priority ST20 process; tasks are
  switched by software. `switch_to` saves Wptr + resume Iptr in the task struct and loads the
  next (`gajw`, `gcall`). The hardware process queues are used only for the tick.
- Tick and preemption (arch/st20/kernel/time.c): a high-priority process waits 1 ms on
  the 1 MHz high-priority timer (`tin`): front-panel refresh every time, a jiffy (HZ=100)
  every 10th time. Waking up it preempts the kernel; like OS20 when an
  interrupt preempts a task (ST20R2.3.1/src/os20/c2c4/ksuspend.c) it saves the kernel's
  state record (enables, status, Wptr, Iptr, A-C) with `stshadow` into a frame below the
  kernel's Wptr (or the kernel stack top for user code) and points it at `st20_preempt`
  with `ldshadow`. st20_preempt runs do_timer, IRQ work, bottom halves and schedule() at
  low priority and resumes the interrupted code with `restart`. Works at any instruction
  boundary. schedule() runs there only when user code was interrupted: 2.0 kernel code is
  not preemptible (a task between `state = TASK_INTERRUPTIBLE` and its own schedule(), e.g.
  n_tty write_chan, would leave the run queue for good); kernel code reschedules on the
  way back to user mode (st20_syscall_c checks need_resched). The timeslice trap is NOT used: gcc-t800 loops often close with `cj`, which is
  no timeslice point, and a timeslice also needs a second queued low-priority process.
- `cli`/`sti` = `gintdis`/`gintenb` of the three low-priority enables (process interrupt,
  timeslice, external event): high priority cannot preempt a critical section.
- Device interrupts (arch/st20/kernel/irq.c): Linux IRQ = STi5512 interrupt source (ASC3 =
  7, PWM/IR = 11). Each requested source gets its own INC level via the INL (0x20011000);
  the INC (0x20000000) starts `st20_irq_entryN` (entry.ast) at high priority. A source may
  have a fast high-priority top half (`st20_request_fast_irq`: ASC receive ring, IR edge
  timestamps); otherwise the level is masked until the Linux handler has run. Linux
  handlers run at low priority from st20_preempt (same redirect as the tick). The tick
  turns high-priority external events off while it redirects.
- Error traps (traps.c): low-priority trap groups 1 (integer error) and 2 (system: illegal
  opcode; records 0x800000e0/0x80000100) kill a faulting user program with SIGFPE/SIGILL
  and panic in kernel code. Integer overflow (incl. division by zero) is not trapped: the
  same trap fires on add/adc overflow, which gcc-t800 emits for wrapping arithmetic.
- Signals (signal.c): user handlers get a context (old mask, Wptr, Iptr, A-C) below the
  interrupted Wptr and run as if called from `st20_sigtramp` (kernel text, user mode),
  which calls sigreturn. Delivered at the end of system calls and of preemptions that
  interrupted user code (so Ctrl-C stops busy loops). Exec, sigreturn and handlers resume
  user code with a state record and `restart` (`st20_resume_user`). sigsuspend restores its
  mask after the handler. -ERESTART* become -EINTR (no syscall restarting).
- OS20 source (ST20R2.3.1/src/os20, C2 kernel in c2c4/) is the reference for real-C2
  behaviour beyond st20emu.
- Syscalls: no protection without an MMU, so user code calls a fixed kernel entry (vector at a
  known address); the wrapper runs signal/resched checks on return.
- Console: RS-232 on the board ASC (DSR9500: ASC3 0x20006000), set up as the original
  loader does (console.c; Decompilation/DSR9500 src/m_ab80.c asc_init): PIO4 bit 0 TXD,
  bit 1 RXD; baud reload 0x1b (0x20 when config word 0x20000500 & 0x3f == 1) for 115200
  8N1. /dev/ttyS0 (serial.c): receive by interrupt, transmit polled, termios sets baud,
  character size, parity, stop bits (`stty 9600`).
- Instruction cache (arch/st20/mm/cache.c): enabled at boot (invalidate, wait, enable:
  cache block 0x4000 +0x800/+0x700, OS20 cache/sti5512a.c); invalidated by
  flush_cache_mm on exec. The data cache stays off (no cacheable regions).
- RAM: DSR9500 SDRAM 8 MB at 0xc0000000.
- Build: `ld -r` steps become `ar` archives; final `lit` from the boot hook pulls in what
  `start_kernel` references. User binaries: flat (bFLT); relocations by linking twice at two
  addresses and diffing.

### Board: DSR9500 (`include/asm-st20/board.h`)

SDRAM 0xc0000000-0xc07fffff (8 MB), flash 0x7fe00000-0x7fffffff (2 MB), console = ASC at
0x20006000 (shown by st20emu as `ASC3>`), kernel linked at 0xc0001000, system call vector
word at 0xc0000000.

Flash image (`tools/mkrom-dsr9500.py linux.bin dsr9500-uclinux.bin`):

| address | content |
| --- | --- |
| 0x7fe00000 | kernel image (copied to 0xc0001000 at boot) |
| 0x7fe80000 | root romfs image (read in place) |
| 0x7fff0000 | loader: 56 boot pokes with `devsw` (Samsung EMI/SDRAM setup twice around the MinInt fill of 0x80000000-0x8000003c, from Decompilation/DSR9500 boot-tables.md), Wptr = 0x80000700 (on-chip SRAM), `call` the boot init blob, `move` kernel to SDRAM, `gcall` 0xc0001000 |
| 0x7fff1000 | boot init blob (`user/boot/bootinit.c`): Samsung's `dsr_ckg_init` (PLL, clock dividers) and `dsr_sdram_init` (SDRAM controller, read-back calibration), as the original romload calls them after its pokes |
| 0x7fffffe0 | `ldc 0x7fff0000; gcall` |
| 0x7ffffffe | reset entry: `61 00` = `j 0x7fffffe0` (same form as the original `61 07`) |

On real hardware flashing this image replaces the original firmware: back up the flash
first. No watchdog is used by the original firmware, so none is serviced.

### DSR9500 devices

| device | file | notes |
| --- | --- | --- |
| RS-232 terminal `/dev/ttyS0` | console.c, serial.c | ASC3, irq 7, 115200 8N1 default |
| TV text console | osd.c | 80x30, 8x16 font (Linux 2.4 font_8x16), mirrors printk and ttyS0; OSD 2 bpp, one block per field; bring-up as the loader's av_init (Decompilation/DSR9500 src/osd.c) |
| IR remote keyboard | ir.c, `include/asm-st20/irkeys.h` | PWM capture 0x2000b000, irq 11; NEC, customer 0xfe01; types into ttyS0 |
| front panel | panel.c | `/dev/panel` (char 60): write up to 4 characters to the LED display; keys: up/down = cursor keys, power = Enter |

IR keyboard: `tools/mkirkeys.py include/asm-st20/irkeys.h dsr9500-keyboard.ir` generates
the kernel key table and the Flipper Zero remote file `dsr9500-keyboard.ir` (raw NEC frames;
153 buttons) from one table:

| NEC command | key |
| --- | --- |
| 0x00-0x1f | the DSR9500's own remote: digits, OK = Enter, Exit = Ctrl-C, arrows, P+/P- = PageUp/PageDown, Menu = Tab, Power = Esc |
| 0x20-0x7e | that ASCII character |
| 0x7f | Backspace (DEL) |
| 0x81-0x9a | Ctrl-A ... Ctrl-Z (0x88 Backspace, 0x89 Tab, 0x8d Enter), 0x9b Esc |
| 0xa0-0xa9 | Up, Down, Right, Left, Home, End, PageUp, PageDown, Delete, Insert |

Copy `dsr9500-keyboard.ir` to the Flipper's SD card (`infrared/`). In st20emu:
`ir 0x6c 0x73 0x8d` types `ls` Enter; `fp power` presses the panel's power key;
`screen tv.bmp` saves the TV picture; `--console 3` types into the serial console.

### User space (step 5)

- `user/`: `crt0.ast` (entry: Wptr = sp, sp[0] = argc, argv after it), `sys.c` (system
  call stubs through the vector at 0xc0000000), `init.c`. `user/build-user.sh` (container)
  links each program twice (bases 0 and 0x100000); `tools/mkflat.py` diffs the two images to
  find pointer words and writes a bFLT rev 4 file (FLAT_FLAG_RAM, so text and data stay
  adjacent for gcc-t800's PC-relative data; header, relocations and relocated words
  big-endian as fs/binfmt_flat.c reads them with ntohl).
- `tools/mkromfs.py out.img dir:/dev chr:/dev/ttyS0:4:64 file:/sbin/init:FILE:x ...`
  writes the romfs root; it sits in flash at 0x7fe80000 and blkmem reads it in place.
- Kernel pieces: `arch/st20/kernel/serial.c` (/dev/ttyS0 = major 4 minor 64 on the board
  ASC; TX polled, RX polled every tick), little-endian `asm-st20/byteorder.h` and
  `unaligned.h` (h8300's were big-endian), verify_area accepts SDRAM and flash
  (mmnommu/memory.c), blkmem arena at ST20_ROMFS_ADDR.
- Typing into the console: st20emu's `asc PORT TEXT` CLI/script command queues TEXT and a
  newline on ASC PORT's receiver (DSR9500 console = `asc 3 ...`).

### C library and shell (step 6)

- uC-libc (`src/uclinux-dist/lib/libc`, MACHINE=st20), built by `user/build-libc.sh`:
  - `st20/crt0.ast` (environ, `exit(main(argc, argv, envp))`), `st20/setjmp.ast`
  - `include/st20/`: machine headers; va_* copied from gcc-t800's generic stdarg.h
  - `sysdeps/st20/sysdep.h`: every `SYSCALL_RET(name, n)` stub (*.S) is compiled as a C
    function calling the kernel entry at 0xc0000000; `vfork.ast` (= fork) jumps into the
    kernel with its own frame; C select/ptrace/_cleanup; sigsuspend without m68k asm
  - rpc is left out; `-D__restrict=`
- sash (`user/sash`) with `user/sash-ps-stub.c` (ps.c uses floating point); libsash's
  utils.c is compiled once per `-DL_name`; `user/include/config/autoconf.h` is empty.
- `tools/mkimage.sh` makes the bFLTs, the romfs (/bin/sh, /bin/hello, /etc/rc, /dev, /proc)
  and the flash image. Without /sbin/init the kernel runs /etc/rc and then /bin/sh in a loop.
- vfork on ST20: fork() = vfork (CLONE_WAIT, no CLONE_VM, as m68knommu). The child gets a
  copy of the parent's user stack (arch/st20/kernel/process.c copy_vfork_stack), because
  gcc-t800 shares workspace slots between locals dead on the child's path; the kernel also
  restores the caller's return Iptr from pt_regs on syscall return.
- lit drops fragments nobody needs and every global label starts one: aliases in hand
  assembly must be assignments (`_fork = _vfork`), not a second label.
- Do not compile with -O0: gcc-t800's -O0 code is wrong for some calls (wait4's arguments).
- Soft float and more compiler fixes (2026-11-11, verified against host IEEE results on 63k
  random operations and with egcs-1.1.2 c-torture/execute: 285/318 pass at -O2, the rest
  are nested functions, `__complex__`, designated initialisers and a few reg-stack aborts):
  fp-bit (division by zero, inf signs, denormal rounding, -0+0, double->float sticky bit),
  reload.c/reload1.c (argument and two-word return registers clobbered by reloads),
  t800.md (`ashrdi3` was a logical shift; `_gcall` return address; address registers of
  stnl/sb/ss as inputs in exotic.c), function.c (REG_EQUIV on the second word of an
  addressable double/long long parameter).
- ABI: `short` is 16 bits (a precedence bug in TARGET_SHORT16 made it 32 on t450, which
  broke struct layouts such as minix inodes and termios). Everything was rebuilt after it.
- Toolchain test harness from that work (c-torture runner, float tests vs host) lives in the
  untracked `x-softfloat/` directory (ctest/ctrun.py, fptest/all.sh).
- reg-stack aborts with 16-bit short (2026-11-11): loop.c no longer substitutes a
  computation into an insn setting a hard register (zero-extend moved into the Areg argument
  copy needed four stack slots), combine.c emits `(use)` for unplaced REG_DEAD notes only for
  hard registers, global.c gives no stack register to pseudos live at function entry,
  reload1.c inheritance does not reuse one input's register as another input's reload
  register. Kernel, uC-libc, sash and busybox compile; c-torture -O2 289/318.
  `tools/rebuild-gcc.sh` (in the container) rebuilds and installs cc1/libgcc after edits.
- gcc-t800 predefines `transputer`/`__transputer__` but not `__linux__`: uC-libc headers
  that test `__linux__` also accept `__transputer__` (gnu/types.h etc.).
- Further gcc-t800 fix: local-alloc.c makes no REG_EQUIV to a MEM with a register address
  (`a = b = c` on globals made reg-stack abort).
- Toolchain fixes 2026-11-11/12 (found by the 2.4 port; harness and diffs in
  `x-softfloat/sr` and `x-softfloat/tc`, all in `patches/`):
  - strength-reduce SIGSEGV, reg-stack aborts (double polynomial, `t.string = --p`, igmp.c:
    local-alloc.c REG_EQUIV to a MEM with a non-fixed register address), `__builtin_alloca`
    block overlapping the Wreg save slot (`STACK_DYNAMIC_OFFSET`, t800.md allocate_stack).
  - alloca + calls: t800_expand_call retargets Wreg below the alloca'd blocks (`gajw`,
    gcall, all arguments on the stack) only for calls expanded after the function's first
    alloca.  Calls copied from an inlined function (expanded in its own context) and
    library calls (emit_library_call: no FUNCTION_ARG_PRESCAN) ran on the normal Wreg and
    overwrote the block.  Now calls.c does not inline a function that makes calls once the
    current function has called alloca, and `INIT_CUMULATIVE_ARGS` starts with
    `must_pass_in_stack = current_function_calls_alloca`.  That exposed a generic gcc-2.7.2
    bug: emit_library_call(_value) stored stack arguments at `argvec[count].offset` instead
    of `argvec[argnum].offset` (with PUSH_ARGS_REVERSED: reversed arguments).  Still not
    handled: a call expanded BEFORE the first alloca inside a loop that also contains the
    alloca runs on the normal Wreg on the next iteration; cc1 warns ("alloca in a loop
    after a call", explow.c), the fix is an alloca at the top of the function (uClibc
    glob.c has one).  Runtime test `x-softfloat/sr/rt/alloca2.c`.
  - c-typeck.c push_init_level (generic gcc-2.7.2, as gcc 2.8): a designated initialiser
    that skipped members before a nested `{ }` member got a bogus gap
    (`{ family: 2, parms: { x: 5 } }`), every later value landed too late.
  - ttools ast scan.l/scan.c `mk_string_space`: a string run of more than ~80 characters
    overflowed the heap buffer (grew once per token); now grows until it fits.
  - c-torture -O2 291/318, -O1 290 PASS (`x-softfloat/sr/regress-inst.sh` against the
    installed `tools/`), float/long long tests vs host: 0 mismatches.

### busybox and init (step 7)

- busybox 0.60.5 from uClinux-dist (`src/uclinux-dist/user/busybox`, in the sparse
  checkout), configured by `user/busybox-config.h` (a copy of its Config.h: applets, no
  networking, msh as `sh`). `user/build-user.sh` compiles the applet sources named by the
  config's `BB_*` macros (like busybox.sh), libbb as an archive and the libbb messages once
  per `-DL_name`, links `build/busybox` and writes `build/busybox.links` (applet paths, as
  busybox.mkll). `build-user.sh busybox` rebuilds only busybox, recompiling changed files.
- Applets: `[ basename cat chmod clear cmp cp cut date dd df dirname dmesg du echo egrep env
  false find free grep halt head hexdump init kill killall ln ls md5sum mkdir mkfs.minix
  mknod more mount msh mv pidof poweroff printf ps pwd reboot reset rm rmdir sed sh sleep
  sort stty sync tail tee test touch tr true tty umount uname uniq uptime usleep vi wc
  which xargs yes`.
- Shell: msh (ash is "not even a chance" on uClinux, hush/lash fork). msh vforks and
  saves/restores what the child changes. No job control: `cmd &` prints the pid, `kill` it.
- `tools/mkimage.sh` puts `/bin/busybox` into the romfs and every applet as a romfs hard
  link (`tools/mkromfs.py link:PATH:TARGET`, romfs type 0, resolved by the kernel's
  romfs_lookup, so busybox sees the link name in argv[0]); sash stays as `/bin/sash`.
  `/lib/liberror.txt` holds uC-libc's strerror() texts (it reads them from that file).
- Boot: the kernel runs `/sbin/init` (busybox init). `user/rootfs/etc/inittab`: sysinit
  `/bin/sh /etc/rc` and a respawning login shell `-/bin/sh` on ttyS0 (`/etc/profile`:
  PATH, PS1). Both name ttyS0 explicitly: `/dev/console` (5,1) is the TV console, where
  init's own messages go. `/etc/rc` mounts /proc, makes a 512 KB minix file system on
  `/dev/ram0` and mounts it on /tmp, writes "uCL " to `/dev/panel` and prints a welcome.
- /dev: console (5,1), tty (5,0), ttyS0 (4,64), null, zero, panel (60,0), ram0/ram1 (1,0/1),
  rom0 (31,0, the root romfs; df's root device).
- uC-libc changes for busybox: POSIX `<regex.h>` (`lib/libc/regex/regex.c`, a small
  backtracking matcher: BRE/ERE, brackets with classes, intervals, groups, alternation,
  backrefs, REG_ICASE/NEWLINE/NOSUB/NOTBOL/NOTEOL; leftmost, then first alternative /
  greedy, not POSIX leftmost-longest) instead of the V8 regexp for MACHINE=st20 (both
  define regcomp); `_NSIG` in bits/signal.h; printf `%e %f %g` (stdio2 `-DFLOATS=1`, `math/`
  gcvt built again, needs libgcc's soft float; `%g` prints like `%f`).
- gcc-t800 predefines only `transputer`, no `__linux__`: gnu/types.h, waitstatus.h,
  waitflags.h and sys/uio.h test `defined(__linux__) || defined(__transputer__)`. Without it
  gnu/types.h made `__dev_t`/`__uid_t`/`__gid_t`/`__mode_t` ints and struct stat no longer
  matched the kernel's once short became 16 bits.
- busybox source changes: grep's `reflags` global (-E/egrep were broken upstream), grep and
  xregcomp use regex.h on `__transputer__`, new `hexdump.c` applet (canonical -C output,
  -s, -n), msh keeps the backslash in `"\n"` (POSIX: inside double quotes it quotes only
  dollar, backquote, double quote, backslash and newline; msh dropped it, so
  `printf "a\n"` printed `an`), `BB_FEATURE_NO_FLOAT` option for printf/more (off),
  busybox.c maps an empty argv[0] to `sh` (kernels before the binfmt_script NO_MM fix
  passed "" for `#!` interpreters).
- Tested in st20emu: init -> rc -> msh prompt; files, hard links, dirs in /tmp; pipes
  (`ls / | wc -l`, `sort | uniq -c`, `cut | tr | tee`), grep/egrep/sed with groups, find |
  xargs, vi (insert, `:wq`), more, `sleep 100 &` + ps + kill/killall, Ctrl-C on `sleep 100`,
  /proc/meminfo, free, df, mount/umount of a second minix RAM disk, sash and sigtest.
  `head -3` is not supported by this busybox (use `head -n 3`); ps shows Size 0 (no VM
  sizes on NOMMU); date's %Z prints `???` (no time zone data).

### Build and run

```sh
docker run --rm --platform linux/386 -v $PWD:/work st20-build /work/kbuild.sh oldconfig dep  # once
docker run --rm --platform linux/386 -v $PWD:/work st20-build /work/kbuild.sh   # ~6 min clean
docker run --rm --platform linux/386 -v $PWD:/work st20-build bash -c \
  'cd /work/src/uclinux-dist/linux-2.0.x && /work/tools/bin/crossload -o linux.bin linux'
docker run --rm --platform linux/386 -v $PWD:/work st20-build /work/user/build-libc.sh
docker run --rm --platform linux/386 -v $PWD:/work st20-build /work/user/build-user.sh
tools/mkimage.sh                                   # -> dsr9500-uclinux.bin
../../builddir-release/st20emu --soc sti5512 --board dsr9500 --firmware dsr9500-uclinux.bin
# at the prompt: g (run), then stop with g and type e.g. "asc 3 ls -l /bin", g again;
# or script it: --script FILE with lines "run 400000000", "asc 3 echo hi", "run 60000000"
```

`relink.sh "-T 0xc0001000 -mlinux.map"` (in the container) relinks only (~15 s).

### Build notes (step 1)

- `kbuild.sh [targets]` runs the kernel make in the container (ARCH=st20, CROSS_COMPILE=t450-).
- `tools/bin/st20-ldr` (= t450-ld): `ld -r` makes an ar archive; the final link flattens
  nested archives (lit reads only plain ones) and runs lit.
- The assembler is plain `ast` (t450-as and gcc's `as`). Segments text, data, bss are
  laid out back to back by head.ast, which must create them in that order (`.text .data
  .bss` at the top): lit writes segments in first-seen order and crossload concatenates
  them, so a bss-before-data order silently zeroes all initialised data.
- Hand-written assembly is `*.ast`: the kernel's `make clean` deletes every `*.s`.
- ttools MUST be built with `-fwrapv`: `sum_overflows()` in shared/expr.c tests signed
  overflow with `a+b < 0`, which gcc 4.3 -O2 deletes (UB), so range estimates wrapped and
  ast/lit gave far calls 2-byte slots ("slot 2 is too narrow", assertion "ranges should
  never grow"). libgcc.a must be assembled with the fixed ast. gcc is built with -fwrapv
  too.
- gcc-t800 bug fixed in config/t800/t800.c `t800_expand_compare`: QImode/HImode ordered
  comparisons with a small constant were zero-extended, so `(signed char) -1 < 0` was false
  (printk's log-level parsing broke). Now they are sign-extended; only EQ/NE (and the
  unsigned forms) zero-extend.
- `lit` prints growing elements when `LIT_DEBUG_GROW=1` (shared/ments.c); the "too narrow"
  error names fragment, opcode and argument.

### Steps

1. [x] toolchain + bare-metal hello on st20emu
2. [x] `arch/st20` + `include/asm-st20`, build with t450-gcc/ast/lit/ar; full kernel
   compiles and links (2026-11-11): `linux` (LIT) -> `linux.bin` (276 KB, at 0xc0001000)
3. [x] head, console, printk
4. [x] tick, preemption, switch_to, scheduler (2026-11-11): calibrates (8.06 BogoMIPS),
   prints the "uClinux version 2.0.39.uc2" banner, starts the init thread, which stops at
   "VFS: Unable to mount root fs on 1f:00" (no root filesystem yet)
5. [x] romfs root + `/sbin/init` (no libc) from user space (2026-11-11): flat binary with
   relocations, write/read/getpid system calls through the vector, `/dev/ttyS0` with echo
6. [x] uC-libc + sash shell (2026-11-11): /etc/rc mounts /proc, interactive `/>` prompt
   on ttyS0; built-ins, external programs (vfork + exec + wait), /proc all work
7. [x] busybox 0.60.5 init + msh, /etc/inittab and /etc/rc, writable /tmp (minix on the
   RAM disk), signals and error traps, interrupt-driven ttyS0, TV text console, front panel
   (/dev/panel), IR remote keyboard (dsr9500-keyboard.ir for Flipper Zero), I-cache on,
   soft float (2026-11-11)

2026-11-12: kernel (425,416 bytes), uC-libc, sash and busybox rebuilt clean with the fixed
toolchain (designated initialisers, ast strings, alloca/libcall fixes above); boots in
st20emu to the msh prompt (uname, ls, /tmp write + read, sed, pipes, `sleep &` + ps +
killall, sash, sigtest).
