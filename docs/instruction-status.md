# ST20 instruction implementation status

Updated: 2026-10-12.

Instruction list: [ST20 C2/C4 reference manual JSON](../manuals/ST20/ST20_C2_C4-Core-Instruction-Set-Reference-Manual--72-TRN-273-01.json).
Implementation: [instruction handlers](../src/core/cpu/instructions.cpp), [dispatch table](../src/core/cpu/instrentry.cpp), and [CPU helpers and decoder](../src/core/cpu/st20.cpp).

## Status definitions

- **Implemented**: the core operation has an active implementation. This does not certify complete hardware fidelity or every error/device integration path.
- **Partial**: an active implementation exists, but known instruction behavior is missing or limited.
- **Stub**: the handler changes stack state or prints a message without implementing the instruction's main effect.
- **Not implemented**: dispatch uses `invalidOp_`, or no usable encoding has been established. Independently corroborated encodings are noted when the primary reference is defective.

The 171 reference instructions comprise **151 implemented**, **20 partial**, **0 stubs**, and **0 not implemented**. Encoding components are listed separately below. Partial includes functional portable-mode behavior whose silicon-specific conventions remain unverified.

Instruction timing remains approximate. Newly added handlers use a baseline cycle count of one, not a hardware-verified timing value. Undefined bit-reversal widths and invalid `xword` inputs use the existing undefined-word marker, 0xcccccccc; this is an emulator policy, not a claim about silicon-specific undefined behavior.

## R2.3.1 simulator verification

The requested `ST20R2.3.1/bin/st20sim.exe` identifies itself as version 7.32
(built May 5, 2007). Two seeded rounds with 17 boundary and 32 random cases
per fixture, plus trap-mask and captured-shadow fixtures, passed **16,856 cases
and 1,066,130 defined-value comparisons across 165 instructions**, with no
divergences. Local ASan/UBSan tests passed 473,292 checks. These comparisons
use the harness's defined-value filtering, frozen clocks and fixture-specific
scope; they do not certify silicon timing, peripheral behavior or full boot.
Traces and reports are retained under `obj/tests/st20sim-*`. Newly generated
reports identify the simulator executable/version and count blocked cases
separately from completed comparisons.

The oracle does not decode `lddevid`. Standalone `iret` requires controller
context: initializing its previously unmapped pointer removes that fault,
but the controller returns from level -1 and goes idle rather than reading
the prepared frame. `reboot` (now a boot-from-ROM cold reset) has not been
compared with the simulator. All five copy
families still yield only boundary captures with operation code zero and
undefined auxiliary words in DLL queue/signal probes. Timer-event-file
probes crash simulator 7.32 with internal `sig=11`; the new `--interrupted 5`
probe returns `EVENT_TICK_HIGH` from the second memory read but never enters
the capture handler. DLL probes used the R2.3.1 headers through
`ST20SIM_INCLUDE`. No active native continuation layout or physical channel
mapping was established, so the **20 partial classifications remain** and
no CPU behavior was changed on the strength of these probes.

## Native floating-point helpers

`postnormsn` and `roundsn` are implemented in native mode without enabling the
portable ABI. They support biased-exponent normalization correction, denormal
guards, nearest-even rounding, subnormal carry and overflow. Sign application
and special-value classification remain the caller's responsibility. The
opt-in portable format is separate; hardware timing remains unverified.

## Native copies and STi5518

Native `move` accepts the full unsigned 32-bit count. `move` and `move2dall`
use aligned four-byte transfers when both cursors permit them, with byte
prefixes, tails and a byte fallback for differing alignments. Transfers stay
within each requested span and row; errors retain only completed writes.
Filtered 2D copies use aligned word reads when source and destination can reach
the simulator's word boundary with the same byte prefix. Fully matching words
use one word write; mixed words are re-read bytewise and only matching bytes
are written. Differently aligned rows use byte accesses. Exact silicon bus
scheduling remains unverified.
Exact silicon bus schedules and native interrupted-operation restart remain
unverified, so these instructions are still Partial.

The [STi5518 datasheet](../manuals/STi5518/STi5518-Datasheet--7170179-D.pdf),
section 3.3, explicitly retains interruption/restart of long-running CPU
instructions. Its section 27 block-move DMA is a separate register-driven
peripheral (1-65535 bytes per transfer), not a replacement for CPU copy
instructions or their saved state. The current STi5518 device model does not
implement that DMA engine, and its native IRQ path still rejects unfinished
CPU microcode. STi5518 therefore does not resolve these emulator limitations.

## Portable emulator ABI

The user explicitly authorized an emulator-specific ABI for undocumented formats. Enable it with `PORTABLE_INSTRUCTION_ABI = 0x1` in st20emu.ini, or `setPortableInstructionAbi(true)` for an embedded core. It is off by default. **These conventions are not asserted to match ST20 silicon and can break existing firmware.** The reference/native limitations described later in this document remain applicable when portable mode is disabled, except for the current snapshot format.

### Saved state and continuations

Portable records contain twelve 32-bit words. Word 0 transfers enable bits 16-23 and trap bits 0-13, preserving other destination bits on stshadow. Word 1 is Status; words 2-3 are Wptr/Iptr; words 4-6 are A/B/C. The five block words are 7=row width, 8=rows remaining, 9=destination stride, 10=source stride, 11=cached read word. For pending channel copies, words 9-10 instead hold channel address and peer descriptor; NotProcess.p as the peer identifies an external FIFO endpoint.

Status bit 25 tags the portable format. Bit 24 transports the private intdis flag. Bit 21 marks a successfully read but not yet committed word; bits 22-23 encode its width minus one. Bits 26-30 identify pending operations: 1=move, 2=devmove, 3=move2dall, 4=move2dzero, 5=move2dnonzero, 6=input copy, 7=output copy. A/B/C hold row bytes remaining, destination cursor, and source cursor while a copy is pending. These values become undefined on completion. Other native microcode operation codes are not accepted as portable continuations.

Copies checkpoint after committed transactions and can be preempted by high-priority work or queued IRQs. A cached successful read is retained across write faults, restart, and snapshots, so retrying does not repeat that MMIO read. Filtered 2D copies use byte transactions; other copies use up to four bytes per transaction. Abstract bus cycle accounting remains approximate. Short ALT/timer/semaphore bookkeeping is an atomic portable transaction: its guest workspace state and next Iptr provide continuation after blocking, not an invented replica of native internal microcode phases.

Portable ldshadow/stshadow support all four selector groups, captured trap callers, inactive high/low banks, and block-only current-bank loads. Edits to captured caller control state also update the paired trapped record. Current-bank context loads remain rejected because the reference declares them undefined. Portable restart requires a tagged record, validates the complete candidate before changing CPU state, restores the current priority's context/masks, and abandons the active trap stack. It does not implicitly enqueue the replaced process. Silicon restart layouts and return-bank conventions remain unverified.

### Floating-point conventions

For postnormsn, A/B contain the norm result's low/high significand words and C its shift count (0-64). pw.Temp holds sign in bit 31 and a signed 31-bit, unbiased pre-normalization exponent in bits 0-30. The helper subtracts C from that exponent, preserving sign, and leaves the normalized significand in A/B. A zero significand preserves signed zero. Exponents outside the representable portable range are clipped.

For roundsn, B:A is a normalized unsigned 64-bit significand with its leading bit at bit 63, or zero; C holds the same sign/signed-unbiased-exponent encoding. Its mathematical magnitude is `(B:A / 2^63) * 2^exponent`. Integer nearest-even rounding packs IEEE single precision into A, including gradual underflow, signed zero, carry into the next exponent, and overflow to infinity. B/C become undefined. Non-normalized nonzero inputs are rejected. NaN/infinity classification and special arithmetic remain caller responsibilities. This is an emulator-specific numerical contract, not the native postnormsn/roundsn format.

### Hardware event models

`queueExternalInterrupt(level, workspace, iptr, status)` queues one of eight portable levels (7 highest). Delivery occurs at instruction or copy checkpoints, observes the appropriate global external-event mask, and nests only at a higher level. The supplied workspace is the save-frame base, not a guessed chip interrupt-vector register. Managed iret restores high, low-shadow, idle, or pending-copy context and the controller stack. Interrupt handlers cannot deschedule or perform channel communication/block moves. The register-level STi5518 interrupt controller is not emulated by these APIs.

Standalone startup additionally installs a separate native STi5518 ASC1/ASC2/controller model. ASC1 (`0x20004000`, source 9) and ASC2 (`0x20005000`, source 8) have independent registers, transmit delays and captures. Their readiness is routed through firmware-programmed source/level masks, trigger modes and vector registers; sources sharing a level are combined before evaluating its trigger. `serviceNativeInterrupt` loads handler Status/Iptr from HandlerWptr-8/-4 and uses the fixed save-area base HandlerWptr-32; a low-priority caller is held in shadow registers, while a high-priority caller uses the six-word save frame. Vector bit 0 controls priority relative to high-priority processes, global external-event masks and low-priority interruption disabling defer entry, and iret acknowledges the controller's executing level. Interrupted native operations remain unsupported. Transmit bytes are captured by `takeSti5518Asc1Transmit` and `takeSti5518Asc2Transmit`; readiness uses a short instruction-boundary delay rather than baud-accurate timing. Incoming data, full FIFO operation, unrelated peripherals and chip-state snapshots are not implemented. Peripheral polling and interrupt-return callbacks are owned by this board model while installed. The STi5512 device model uses the same `serviceNativeInterrupt` entry and iret acknowledgement for its interrupt controller.

`registerExternalChannel(address, capacity)` installs a bounded FIFO endpoint in either native or portable mode; `feedExternalChannel` supplies incoming bytes and `drainExternalChannel` consumes outgoing bytes. No portable ABI opt-in is required for registration, ALT guards or reset. Endpoint addresses are explicit host configuration, not a recovered STi5518 hardware channel map. Each message must fit the configured capacity; a channel permits one active transaction or waiter.

Native FIFO transfers are atomic and use RAM buffers only. Wrapping spans, peripheral spans and mapped-device buffers are rejected before FIFO consumption. Input bytes are removed only after all RAM writes succeed; output bytes are published only after all RAM reads succeed. Empty input or insufficient output capacity parks the process, and the transfer completes before its owner is released. ExternalChannel traps leave enqueueing to the kernel. Native wakeups do not construct tagged portable records or interrupted-operation Status codes. Ready/blocking input, output, scalar sends, both priorities, ALT wakeups, reset, failure preservation and blocked/completed-transfer snapshots have local sanitizer regressions; this is not an oracle-verified physical-channel implementation.

Portable FIFO transfers retain the resumable copy and cached-read behavior described above. In both modes, ALT enables/disables and readiness notifications work with configured endpoints. resetch clears FIFO/registration state and returns a canceled waiting descriptor without automatically enqueueing it. Conflicting callers are rejected. Resetting an active portable transfer cancels its model state and may make that continuation fail. FIFO bytes, waiting owners and pending completion notifications retain the existing EXCH snapshot format.

### Snapshots and limits

Version-11 snapshots use 32-bit CPU/register words in the prefix and a 550-word fixed runtime extension: timing data, cached move fields, inactive high state, eight pending IRQ requests and up to eight IRQ frames. An optional EXCH extension serializes FIFO bytes, capacities, active/waiting descriptors, and ALT notification state. Limits are 1024 endpoints, 1 MiB capacity per endpoint, and 16 MiB total queued data per snapshot; malformed state is validated before restoring the live machine. Only version 11 is supported; unversioned files, all other versions (including version 10), and incomplete runtime blocks are rejected without changing live state. Portable ABI configuration and arbitrary MMIO callback objects are board configuration and are not serialized. Guest RAM still must be saved/restored separately; native byte order, boolean representation, and structure padding keep files host-ABI dependent.

Native five-word register interpretation, exact bus/timing behavior, chip-specific event registers, and interrupted native operation codes remain unverified. They are not hidden by the portable implementation. The native FP helpers above are separate from the opt-in portable FP format.

Timer-driven st20sim probes reach the high-priority capture process for all five copy families, but only before the copy executes: the saved operation code is zero and all five block words are undefined. These boundary captures do not establish native in-progress microcode or its restart layout; the partial classifications remain unchanged.

DLL-backed memory probes also remain boundary-only in the tested simulator 6.8 setup. They record Status/A/B/C/Iptr/Wdesc during the second LP source read and request HP work either immediately (`--interrupted 3`) or through controller-level signal polling (`--interrupted 4`). All five copy families reach the HP capture process only at an instruction boundary, with operation code zero and five undefined block words. Signal polling occurs after the copy's reads, not at the requested second-read point. These results do not recover an active native layout and are not counted as equivalence passes.

The optional DLL probes require `--priority 1`, a 32-bit Windows-capable clang and `lld-link`, and the toolset's `simdll.h`. `ST20SIM_CLANG`, `ST20SIM_LINKER` and `ST20SIM_INCLUDE` override their locations; the default include path is the sibling ST20R1.8.1 toolset. Built DLLs and callback diagnostics are retained with each probe. Instruction-level debug signals entered a debugger polling loop, so the retained signal variant uses level 4 without that debug handler. A different oracle or a documented native microcode register format is still needed before implementing the active five-word layout.

## Instructions

Limitations in the table describe native mode unless explicitly labeled portable.
Portable-mode continuations and event models are specified above; a Partial label
does not mean those portable implementations are absent.

| Instruction | Status | Remaining work / notes |
| --- | --- | --- |
| `adc` | Implemented | Checked signed 32-bit immediate addition with overflow trap/error fallback. |
| `add` | Implemented | Checked signed 32-bit addition with overflow trap/error fallback. |
| `ajw` | Implemented | Adjusts the real 32-bit workspace pointer with wrapping word-offset arithmetic. |
| `alt` | Implemented | Begins alternative enabling without changing the integer stack. |
| `altend` | Implemented | Branches by the selected workspace offset from the following instruction. |
| `altwt` | Implemented | Initializes selection state and blocks only if no guard is ready. |
| `and` | Implemented | - |
| `bcnt` | Implemented | - |
| `bitcnt` | Implemented | Counts all 32 bits and wraps the sum. |
| `bitrevnbits` | Implemented | Reverses the selected low 0-32 bits; clears the other result bits. |
| `bitrevword` | Implemented | Reverses all 32 bits, preserving B and C. |
| `bsub` | Implemented | - |
| `call` | Implemented | Saves the call frame in guest memory and adjusts Wptr without allocation bounds. |
| `causeerror` | Implemented | Delivers enabled user-generated causes and records the causeerror Status flag. |
| `cb` | Implemented | Checks signed eight-bit range without changing the stack; IntegerError trap/error fallback. |
| `cbu` | Implemented | Checks unsigned eight-bit range without changing the stack; IntegerError trap/error fallback. |
| `ccnt1` | Implemented | Checks unsigned B in 1..A inclusive, pops the bound, and signals IntegerError on failure. |
| `cflerr` | Implemented | Detects IEEE single-precision infinity/NaN bit patterns, preserves the stack, and signals IntegerError. |
| `cir` | Implemented | Checks signed C in A..B inclusive, retaining C in A; IntegerError trap/error fallback. |
| `ciru` | Implemented | Checks unsigned C in A..B inclusive, retaining C in A; IntegerError trap/error fallback. |
| `cj` | Implemented | - |
| `clockdis` | Implemented | Clock-enable register update. |
| `clockenb` | Implemented | Clock-enable register update. |
| `clrhalterr` | Implemented | Clears the halt-on-error mode flag without changing stack or error flags. |
| `crcbyte` | Implemented | Updates the checksum from A's most significant byte using the polynomial in C. |
| `crcword` | Implemented | Updates the checksum from all 32 bits of A using the polynomial in C. |
| `cs` | Implemented | Checks signed sixteen-bit range without changing the stack; IntegerError trap/error fallback. |
| `csngl` | Implemented | Checks that B is A's signed extension word, retains A and consumes B (old C moves into B); IntegerError trap/error fallback. |
| `csu` | Implemented | Checks unsigned sixteen-bit range without changing the stack; IntegerError trap/error fallback. |
| `csub0` | Implemented | Checks unsigned B in 0..(A-1), pops the bound, and signals IntegerError on failure. |
| `cword` | Implemented | Checks signed B in -A..(A-1) using A's power-of-two sign-bit magnitude, including 0x80000000; pops the bound. |
| `devlb` | Implemented | Ordered unsigned byte load from RAM or a registered MMIO device; device side effects and errors supported. |
| `devls` | Implemented | Ordered unsigned sixteen-bit load from RAM or a registered MMIO device; one width-preserving device transaction. |
| `devlw` | Implemented | Ordered word load from RAM or a registered MMIO device; one width-preserving device transaction. |
| `devmove` | Partial | Ordered RAM/external-MMIO copying in four-byte chunks with exact tails; on-chip peripherals excluded. Interruptible restart and target-specific bus cycles remain unsupported. |
| `devsb` | Implemented | Ordered byte store to RAM or a registered MMIO device; masks B to eight bits and consumes two stack words. |
| `devss` | Implemented | Ordered sixteen-bit store to RAM or a registered MMIO device; masks B to sixteen bits and consumes two stack words. |
| `devsw` | Implemented | Ordered word store to RAM or a registered MMIO device; one width-preserving device transaction. |
| `diff` | Implemented | Unchecked 32-bit subtraction. |
| `disc` | Partial | Disables software or explicitly configured FIFO guards and selects the first ready guard; physical hardware-channel mapping remains unverified. |
| `diss` | Implemented | First enabled skip guard selects, following the description rather than the reversed printed branch. |
| `dist` | Partial | Selects a ready timer in disabling order and detaches outstanding timers, including native FIFO/timer races and wraparound; native microcode restart remains unsupported. |
| `div` | Implemented | Signed division, defined stack effects, overflow trap/error flag, and halt fallback. |
| `dup` | Implemented | - |
| `enbc` | Partial | Enables software or explicitly configured FIFO guards and detects readiness; physical hardware-channel mapping remains unverified. |
| `enbs` | Implemented | Marks an enabled skip guard ready without changing registers. |
| `enbt` | Implemented | Records the earliest enabled timer guard with wrapping time comparisons. |
| `endp` | Implemented | Retires non-final parallel branches; the final branch enters the successor directly at the current priority. |
| `eqc` | Implemented | - |
| `fmul` | Implemented | Signed Q1.31 fractional multiplication with nearest-even rounding; -1 times -1 signals IntegerOverflow with an undefined-word result. |
| `fptesterr` | Implemented | Pushes true for the modeled CPU without an FPU; leaves integer error flags unchanged. |
| `gajw` | Implemented | Exchanges A and Wptr without overwriting workspace memory. |
| `gcall` | Implemented | - |
| `gintdis` | Implemented | Clears selected bits in the 16-bit global-enable field and returns its previous value; only the documented low eight event bits currently affect execution. |
| `gintenb` | Implemented | Sets selected bits in the 16-bit global-enable field and returns its previous value; only the documented low eight event bits currently affect execution. |
| `gt` | Implemented | Signed 32-bit comparison. |
| `gtu` | Implemented | Unsigned comparison. |
| `in` | Partial | Software rendezvous and configured native FIFO input, blocking/wakeup, ALT and trap delivery; physical hardware mapping and native microcode restart remain unsupported. |
| `insertqueue` | Implemented | Prepends the selected priority's list, joins its tail to the old front, and preserves the old back; empty input is a no-op. |
| `intdis` | Implemented | Suppresses high-priority process preemption and pending high-priority timer kernels until reenabling or descheduling; preserves the stack and global mask. |
| `intenb` | Implemented | Clears the low-priority interruption-disable state; pending high-priority work can run at the next scheduler boundary. |
| `iret` | Partial | Restores saved high-priority state or low shadow/idle state and notifies a registered controller observer; native interrupt entry and controller acknowledgement are wired through `serviceNativeInterrupt`. Interrupted microcode restart remains unsupported. |
| `j` | Implemented | Relative jump and periodic timeslicing point; enabled `j 0` delivers a breakpoint trap. |
| `ladd` | Implemented | Checked signed carry-in addition with overflow delivery. |
| `lb` | Implemented | - |
| `lbx` | Implemented | - |
| `ldc` | Implemented | - |
| `ldclock` | Implemented | Reads the explicitly selected clock register. |
| `lddevid` | Implemented | Product-ID alias; decoder remap reaches its handler. |
| `ldiff` | Implemented | Unsigned difference and borrow. |
| `ldinf` | Implemented | Pushes the IEEE single-precision positive-infinity bit pattern, 0x7f800000. |
| `ldiv` | Implemented | Unsigned double-word division and overflow delivery; A/B remain unchanged on overflow. |
| `ldl` | Implemented | Reads guest memory at a wrapping word offset from Wptr. |
| `ldlp` | Implemented | Computes a wrapping word-offset address without allocation bounds. |
| `ldmemstartval` | Implemented | Loads the configured memory-start value. |
| `ldnl` | Implemented | - |
| `ldnlp` | Implemented | - |
| `ldpi` | Implemented | - |
| `ldpri` | Implemented | Loads the explicit current-process priority. |
| `ldprodid` | Implemented | Loads the configured product ID. |
| `ldshadow` | Partial | Loads selected enables, ordinary low-shadow context/stack and idle block records, including captured LP control/register edits, invalidation and reactivation in HP traps. Interrupted native operations remain unsupported. Native block records are words 7-10. |
| `ldtimer` | Implemented | Selects the current process priority's clock independently of workspace bookkeeping. |
| `ldtraph` | Implemented | Installs four descriptor words or enters the enabled LoadTrap monitor without copying. |
| `ldtrapped` | Implemented | Installs four trapped-process words using the reference's group/priority selectors; LoadTrap monitoring supported. |
| `lend` | Implemented | Always decrements the wrapping word count; old signed count > 1 additionally increments index and branches backward from the next instruction. Follows the description rather than the printed terminal-count omission. |
| `lmul` | Implemented | Unsigned double-word product with addend. |
| `ls` | Implemented | - |
| `lshl` | Implemented | Defined counts supported; out-of-range result uses the undefined-word marker. |
| `lshr` | Implemented | Defined counts supported; out-of-range result uses the undefined-word marker. |
| `lsub` | Implemented | Checked signed borrow-in subtraction with overflow delivery. |
| `lsum` | Implemented | Unsigned sum and carry. |
| `lsx` | Implemented | Loads a sixteen-bit memory value and sign-extends it to 32 bits. |
| `mint` | Implemented | - |
| `move` | Partial | Full unsigned count, aligned word copying and exact byte prefixes/tails; differing alignments use byte fallback. Native interrupted restart and exact bus timing remain unsupported. |
| `move2dall` | Partial | Strided copies with wrapping addresses, aligned word transfers and exact row tails; native interrupted restart and exact bus/timing behavior remain unsupported. |
| `move2dinit` | Implemented | Sets row count and byte strides in process state, preserved by shadowing and snapshots. |
| `move2dnonzero` | Partial | Copies only nonzero bytes; aligned fully nonzero groups use word transfers, mixed groups fall back to byte checks. Native interrupted restart and exact silicon timing remain unsupported. |
| `move2dzero` | Partial | Copies only zero bytes; aligned zero groups use word transfers, mixed groups fall back to byte checks. Native interrupted restart and exact silicon timing remain unsupported. |
| `mul` | Implemented | Checked signed product with overflow delivery and safe host arithmetic. |
| `nop` | Implemented | - |
| `norm` | Implemented | Normalizes B:A as an unsigned double word; returns leading-zero count in C. |
| `not` | Implemented | - |
| `or` | Implemented | - |
| `out` | Partial | Software rendezvous and configured native FIFO output, blocking/wakeup and traps; physical hardware mapping and native microcode restart remain unsupported. |
| `outbyte` | Partial | Software or configured native FIFO byte output with blocking/wakeup; physical hardware mapping and native microcode restart remain unsupported. |
| `outword` | Partial | Software or configured native FIFO word output with blocking/wakeup; physical hardware mapping and native microcode restart remain unsupported. |
| `pop` | Implemented | - |
| `postnormsn` | Implemented | Native wrapping biased-exponent correction, retained denormal guards and underflow cutoff; opt-in portable exponent/sign format retained separately. |
| `prod` | Implemented | Unchecked wrapped product without host signed overflow. |
| `reboot` | Partial | Cold boot from ROM: resets CPU, scheduler, trap, timer and OMR state, lets the SoC reset its device models (`setRebootObserver`), keeps all memory (external RAM survives, as warm-restart mailboxes expect) and restarts at `START_ADDR`. Boot from link (BootFromRom low) is not modelled. |
| `rem` | Implemented | Signed B modulo A; division by zero signals overflow. Follows the description rather than the reversed printed formula. |
| `resetch` | Partial | Resets software words or configured FIFO state and returns the canceled descriptor without waking it; physical hardware-channel reset remains unverified. |
| `restart` | Partial | Restores native records with no interrupted operation, or complete tagged portable records including pending operations; native interrupted-operation format remains unverified. A native restart reads only record words 0-6. |
| `ret` | Implemented | Restores Iptr from the call frame and advances the real workspace pointer. |
| `rev` | Implemented | - |
| `roundsn` | Implemented | Native integer nearest-even fraction packing, subnormal carry and overflow; opt-in portable signed/unbiased format retained separately. |
| `runp` | Implemented | Enqueues normally or passes the descriptor to the selected priority's Run trap kernel. |
| `satadd` | Implemented | Signed addition clipped to the 32-bit integer range, without overflow traps. |
| `satmul` | Implemented | Signed multiplication clipped to the 32-bit integer range, without overflow traps. |
| `satsub` | Implemented | Signed subtraction clipped to the 32-bit integer range, without overflow traps. |
| `saveh` | Implemented | Saves high-priority front/back pointers to two guest words and pops A; second-word address wraps at 32 bits. |
| `savel` | Implemented | Saves low-priority front/back pointers to two guest words and pops A; second-word address wraps at 32 bits. |
| `sb` | Implemented | - |
| `seterr` | Implemented | Sets the current priority's error flag and honors halt-on-error mode. |
| `sethalterr` | Implemented | Sets halt-on-error mode and halts if the current priority already has an error. |
| `settimeslice` | Implemented | Updates low-priority Status bit 20 from boolean A and returns the previous enable state; controls periodic slicing at j. |
| `shl` | Implemented | Counts of 32 or more give zero, as the manual's description states (its formal definition says undefined; OS20 relies on zero). |
| `shr` | Implemented | Counts of 32 or more give zero, as the manual's description states (its formal definition says undefined). |
| `signal` | Implemented | Unchecked permit increment or FIFO waiter release, with enabled Signal trap delivery. |
| `slmul` | Implemented | Signed A times signed B plus unsigned C; returns raw double-word high/low bits. |
| `ss` | Implemented | - |
| `ssub` | Implemented | - |
| `startp` | Implemented | Saves the new process entry address and enqueues it at the current priority. |
| `stclock` | Implemented | Writes the explicitly selected clock register. |
| `sthb` | Implemented | Sets the high-priority queue back pointer from A and pops A. |
| `sthf` | Implemented | Sets the high-priority queue front pointer from A and pops A. |
| `stl` | Implemented | Writes guest memory at a wrapping word offset from Wptr. |
| `stlb` | Implemented | Sets the low-priority queue back pointer from A and pops A. |
| `stlf` | Implemented | Sets the low-priority queue front pointer from A and pops A. |
| `stnl` | Implemented | - |
| `stoperr` | Implemented | Saves Iptr and deschedules only when the current error flag is set. |
| `stopp` | Implemented | Saves Iptr and deschedules, dispatching a ready process or restoring low-priority shadow state. |
| `stshadow` | Partial | Stores selected enable/context/stack groups and idle block records, including captured LP callers in HP traps. Native enable words are fully written; portable mode preserves reserved bits. Active native block records remain unsupported. The native block group is words 7-10 (four words, as OS20's `NO_MOVE2D_WORDS` states against the manual's 7-11); word 11 is portable-only. |
| `sttimer` | Implemented | Initializes and enables both clocks and services due timer alarms and ready processes. |
| `sttraph` | Implemented | Exports four handler words or enters the enabled StoreTrap monitor without copying. |
| `sttrapped` | Implemented | Exports four trapped-process words; StoreTrap monitoring supported. |
| `sub` | Implemented | Checked signed subtraction with overflow delivery. |
| `sulmul` | Implemented | Signed A times unsigned B plus unsigned C; returns raw double-word high/low bits. |
| `sum` | Implemented | - |
| `swapqueue` | Implemented | Exchanges the selected priority's queue with B/C, returning the old front/back in A/B. |
| `swaptimer` | Implemented | Exchanges the selected timer-list front and updates its alarm from the new list. |
| `talt` | Implemented | Initializes alternative state and the TimeNotSet marker. |
| `taltwt` | Partial | Waits for software/configured FIFO guards, skip or timers and installs the earliest deadline; saves resume Iptr before timer publication. Native microcode restart remains unsupported. Uses TP2-corroborated `25 F1`. |
| `testerr` | Implemented | Pushes inverted error state and clears the flag at the current priority; old A/B move into B/C. |
| `testhalterr` | Implemented | Pushes the halt-on-error mode as a boolean, shifting the stack. |
| `testpranal` | Implemented | Pushes the latched analyse-on-reset input; ordinary resets latch false, explicit analysed resets latch true. |
| `timeslice` | Implemented | Explicit FIFO rotation at either priority, or scheduler trap delivery with caller descriptor in B; unaffected by process disable controls. |
| `tin` | Partial | Timer waits, idle advancement, wakeups and timer-kernel delivery; failed resume-Iptr saves leave the caller running and the timer list unchanged. Native interrupted microcode restart remains unsupported. |
| `trapdis` | Implemented | Changes the selected priority's mask and returns its previous value. |
| `trapenb` | Implemented | Changes the selected priority's mask and returns its previous value. |
| `tret` | Implemented | Restores saved masks and the return bank; same-priority returns use saved control, while native cross-priority returns resume the edited captured bank. |
| `unpacksn` | Implemented | Returns a left-aligned significand including the normal hidden bit; denormals use exponent 1, non-finite significands are undefined, and C holds wrapped 4*old-B plus the type code. |
| `wait` | Implemented | Consumes a permit or appends the current descriptor to the semaphore FIFO and suspends. |
| `wcnt` | Implemented | Converts a signed byte offset to its signed word offset and low two-bit byte selector. |
| `wsub` | Implemented | - |
| `wsubdb` | Implemented | - |
| `xbword` | Implemented | Sign-extends the least significant byte, clearing stale upper bits. |
| `xdble` | Implemented | Signed double-word extension. |
| `xor` | Implemented | - |
| `xsword` | Implemented | Sixteen-bit sign extension, including clearing stale upper bits. |
| `xword` | Implemented | Sign-extends B using the power-of-two sign-bit mask in A. |

## Encoding components

These three components are handled inside `decodeNextInstr()`, not by standalone instruction handlers. They are additional to the 171 entries above.

| Component | Status | Implementation |
| --- | --- | --- |
| `pfix` | Implemented | Accumulates positive operand prefixes in the decoder. |
| `nfix` | Implemented | Complements the accumulated operand for a negative prefix. |
| `opr` | Implemented | Decodes the secondary operand and selects its dispatch entry, including the `lddevid` remap. Unsupported secondary instructions remain unimplemented. |

## Timer and process model

The HP clock runs at 1 MHz and the LP clock at 15,625 Hz against the configured `CPU_CLOCK`. Fractional phases retain remainders modulo `CPU_CLOCK`; elapsed-cycle batches generate every tick and clock values wrap at 32 bits. Disabled clocks preserve both value and phase. CPU initialization resets phases; clock-register writes preserve divider phase.

Processes have explicit Wptr and priority fields. `nWptr` is legacy/debug bookkeeping, not a hardware address bound. Ready queues are FIFO within each priority. High-priority dispatch preserves the interrupted low-priority integer registers, Iptr, Wptr, and priority. Preemption occurs after the current instruction completes. When no process is runnable, decoding advances virtual time to the next enabled timer alarm; if none can run, it returns `CPU_IDLE` to the CLI rather than executing stale code.

Timer deadlines use wrapping comparisons within half of the 32-bit counter range. `tin` waits until the clock is strictly after the requested time by scheduling at `time + 1`. Timer and process links reside in guest workspace memory. Enabled timer/run/signal/internal-channel events enter scheduler trap kernels instead of automatically enqueueing the released process; B carries the process descriptor. Software-channel/skip/timer alternatives and periodic timeslicing at j are supported. Configured FIFO guards work in both ABI modes. Physical hardware-channel mapping and native interruptible microcode restart remain incomplete; portable queued interrupts use the model described above.

`tin` and queued `taltwt` save the resume Iptr before publishing timer state. If that save fails, they report the memory error without changing the timer head, alarm or previous tail link, retiring the caller, or putting a timed ALT into Waiting. Native timed FIFO/timer combinations have local regressions at both priorities for channel-first wakeup, expiry after channel readiness, wrapped deadlines, disabling-order selection, cancellation and duplicate-wakeup prevention. These ordinary protocol checks do not establish the saved state of interrupted instruction microcode.

Current snapshots use version 11 as specified in the portable ABI section, using 32-bit CPU/register words in the prefix. Older files are rejected; all fixed runtime fields are required. Guest memory, including trap records, must still be saved/restored separately. Snapshot files remain host-layout dependent.

## Selective shadow transfers

This section describes native-mode transfers. Portable mode additionally supports
the tagged twelve-word records and selector groups described above.

`ldshadow` and `stshadow` use A as a selector, B as priority 0/1, and C as a guest record address. Native selector bit 0 transfers the full global-enable field (bits 16-31) and the selected priority's trap enables (bits 0-13) in record word 0. Portable mode retains its documented eight-bit global field (bits 16-23). Bit 1 transfers Status/Wptr/Iptr in words 1-3; bit 2 transfers A/B/C in words 4-6. Successful instructions leave the current integer stack undefined. Addresses wrap at 32 bits. Empty selections perform no memory access; selector bits above bit 3 are ignored.

Enable-group transfers work in either direction between priorities without replacing the active bank's trap mask. Ordinary state-group loads support the inactive low-priority bank from high priority; stores also support the current bank. Loads read and validate every selected word before committing masks or shadow state. Stores preserve unselected words. Native word 0 is written completely without a destination read; bits 14-15 are zero. Portable stores retain their read/modify/write reserved-bit preservation and uninitialized-word policy. Native invalid-bank exports retain control status with bit 31 cleared. Mapped-device errors propagate. Store failures may leave earlier destination words written, but do not consume the caller's argument registers.

The engine tracks process validity separately from raw Status, so exported Status bit 31 is derived from running/shadow-valid metadata. Loading bit 31 activates or invalidates the low shadow; a context loaded as valid must have an aligned Wptr. An absent inactive bank exports invalid Status, `NotProcess.p` as Wptr, and undefined register values. Stack and context groups can be loaded separately before activation. Updated low shadows and enable masks survive current version-11 snapshots.

High-priority trap handlers may load/store an independent low-priority bank. They may also export an LP caller captured by an HP scheduler trap and edit its Status/Wptr/Iptr, register stack, trap enables and validity. In native cross-priority return, the edited captured bank supplies the process state; these edits survive `tret` and snapshots. Clearing bit 31 makes the bank non-runnable but retains imported control and register data for export and later reactivation. An invalid native return does not revive the caller: the scheduler selects queued work or remains idle. Invalidation clears the native process's private interruption-disable state. Reactivation still requires an aligned valid Wptr. Saved global enables and the trap bank's mask are restored separately. The paired HP trapped record's Wptr/Iptr were undefined in the oracle probes and are not read for this native return path. Native loads do not synchronize that paired record; portable mode retains its separate paired-record synchronization policy.

The `--trapshadow 1` fixture checks HP breakpoint handlers without aligning caller/handler enable masks. `--crossshadow 1 --priority 1` checks captured LP callers with fixed control. Mode 2 is an oracle-only edited-control capture; mode 3 compares edited valid Status/Wptr/Iptr and registers. Modes 4/5 capture invalidation/reactivation and invalid return to queued LP work; modes 6/7 compare those cases with the emulator. Validity fixtures use selector 7 and two seeded rounds, checking invalid data export, valid reactivation and fallback dispatch. Local regressions additionally cover invalid-bank snapshots, failed unaligned activation and idle return without a fallback. Native trapped `runp` captures its original arguments; ordinary untrapped outputs remain undefined. These fixtures do not establish interrupted block-register layouts.

Selector bit 3 transfers five block-move words (7-11) for native contexts with no interrupted operation. Native moves are atomic at instruction boundaries: stores export the undefined-word marker for these dormant microcode registers, and loads read every selected word without changing live move configuration. Block-only transfers are also available for the current bank during traps. The silicon encoding of active block registers is not implemented: selected banks with an interrupted operation and interrupted-operation Status imports return `INTERRUPTED_OPERATION_UNSUPPORTED`. Current-bank context loads and high-priority state access from low priority remain rejected. Enable-only accesses remain available during traps. Abortable instruction microcode and external shadow-record transport of the engine's private interruption-disable flag are not implemented. These restrictions keep both instructions partial.

Native `restart` reads all twelve record words before committing, restores enables and Status/Wptr/Iptr/A/B/C at the current priority, and replaces the current process without enqueueing it. It supports only records with no interrupted operation and validates alignment for valid contexts. Successful replacement abandons the trap stack and rearms queue-empty notification for a running process. The five dormant block words are consumed but not interpreted; move configuration and private interruption state start cleared. Native interrupted-operation restart remains unsupported, including preservation of preconfigured 2D move parameters through these guest records. Portable mode retains its separate tagged continuation layout.

## External interrupt return

This section describes native-mode save areas. Portable managed IRQ frames can
also restore pending copies and controller nesting, as described above.

`iret` reads the external-interrupt save area at Wptr. With Status bit 31 (valid) set, its six consecutive words hold Status, Wptr, Iptr, A, B, and C of the interrupted high-priority process. These words are read and validated before changing the active context. A null/invalid Status restores the complete interrupted low-priority shadow if one exists; otherwise it returns to idle rather than executing stale handler code. The low shadow is consumed exactly once and survives CPU snapshots.

The primary instruction reference prints the frame addresses as `Wptr + 1` through `Wptr + 5`. The implementation uses word offsets, corroborated by the six-word save area in Figure 4.2 of the [TP2 datasheet](../manuals/ST20-TP2/ST20-TP20-Datasheet--42-1674-04.pdf), not overlapping byte-offset reads. That datasheet also specifies the null-Status case for an interrupted idle CPU. The source JSON remains unchanged.

`setInterruptReturnObserver` registers an optional synchronous controller notification. It runs once after a successful return and sees the restored context; malformed frames, memory-read failures, and unsupported returns do not notify it. Callback configuration and external controller state are not serialized. With no observer, CPU restoration still works but no physical controller is acknowledged. Interrupt entry, vector dispatch and level nesting are provided separately by `serviceNativeInterrupt` and the SoC controller models (see the STi5518 controller description above).

Saved Status bits 26-30 indicating an interrupted microcode operation are rejected with `INTERRUPTED_OPERATION_UNSUPPORTED`, including the low-shadow path, rather than silently skipping the unfinished operation. Unaligned frames/workspaces, low-priority callers, and attempts to replace a trap frame are rejected. External interruption of an active trap handler is therefore not yet supported; normal trap handlers must use `tret`. These limitations keep `iret` classified as partial.

## Interruption and explicit timeslicing

`intdis` and `intenb` control low-priority interruption independently of the global interrupt mask. They preserve A/B/C. Disabled interruption holds ready high-priority processes and asynchronous high-priority timer kernels pending; `intenb` releases them at the next scheduler boundary. Descheduling clears the disable state. The emulator treats these instructions at high priority as no-ops, where the reference says they are not meaningful. Trap returns restore the interrupted process's disable state rather than retaining handler changes.

`settimeslice` accepts boolean A at low priority, changes the architectural Status timeslice-enable bit (20), and replaces A with its previous state, preserving B/C. High-priority execution is an emulator no-op policy. New processes and descheduled/resumed processes enable slicing.

The [TP2 datasheet](../manuals/ST20-TP2/ST20-TP20-Datasheet--42-1674-04.pdf), scheduling discussion on printed page 16, specifies 1 ms timeslice periods and a maximum of two periods before yielding at a timeslicing point. The core instruction reference explicitly identifies j as such a point. A CPU-cycle-derived 1 kHz phase advances independently of the clock-register enable bits; current and saved running low-priority contexts accumulate at most two periods. At j, an expired quantum rotates the low-priority FIFO or enters the enabled Timeslice kernel, provided Status bit 20, the global low-priority timeslice bit, and interruption controls permit it. High-priority preemption and pending high-priority timer kernels take precedence; the expired low quantum remains pending across that interruption. Descheduling starts a fresh quantum. An empty queue or disabled slicing retains the expired count for a later eligible point. Snapshots preserve phase and quantum counts.

This implements instruction-boundary periodic fairness with the emulator's approximate cycle counts, not hardware-verified timing. The independent phase and metadata counters are emulator scheduling machinery, not an established guest saved-operation ABI. Timeslicing inside interrupted microcode operations remains unsupported.

Explicit `timeslice` works at either priority regardless of the per-process disable controls. With another ready process at the current priority, it saves the caller's next Iptr, appends the caller to the FIFO, and dispatches the next process. With an empty current-priority queue it does not rotate or change disable controls. A/B/C become undefined in either case. An enabled Timeslice trap passes the caller descriptor in B without enqueueing it; the kernel owns that enqueue decision, and return leaves the caller descheduled so queued work runs first. Invalid handlers preserve the caller's active context and do not requeue it. Trap handlers cannot execute a timeslice that would deschedule them.

## Unpacking and reset analysis

`unpacksn` extracts A's IEEE single-precision fraction bits 0-22 into A and biased exponent bits 23-30 into B, without inserting the implied leading bit. C receives four times the original B, wrapping at 32 bits, plus type 0 for either signed zero, 1 for finite nonzero values (including subnormals), 2 for either infinity, or 3 for NaNs. The sign bit is not returned by this instruction; NaN fraction payload bits are retained without floating-point conversion or arithmetic-error signalling.

`testpranal` pushes the analyse input latched at the last CPU reset, independent of process priority and integer error flags. `initCPUState()` retains the normal-reset API and latches false; board integrations can use `initCPUState(true)` to model an asserted analyse input on reset. Scheduler resets and process switches do not change the machine-wide latch. Current version-11 snapshots preserve it. This does not implement an external analyse-pin controller or the separate processor bootstrap/reboot sequence.

## Fractional multiplication and parallel termination

`fmul` interprets A/B as signed integers scaled by 2^31, rounds the product to the nearest Q1.31 value, and chooses an even least significant bit for exact ties, including negative products. It moves the original C into B and leaves C undefined. The unrepresentable -1 times -1 case produces the configured undefined-word marker and uses the existing IntegerOverflow trap/error/halt fallback. The calculation uses integer arithmetic rather than the host floating-point rounding mode. `fptesterr` pushes true because this CPU model has no FPU; it does not test or clear integer errors.

`endp` addresses the successor record through A: pp.IptrSucc is word 0 and pp.Count is unsigned word 1. A non-final branch decrements the count, retires without saving its Iptr, and dispatches another process or restores interrupted low-priority state. With count 1, it enters the successor workspace and Iptr directly, preserving priority and leaving the count at 1. A/B/C become undefined. A zero count follows the formal unsigned decrement and wraps; valid parallel constructs must initialize a nonzero branch count. Memory-access failures preserve the active CPU context before retirement, and trap handlers cannot terminate through `endp`.

## Device access

The six device load/store instructions use the shared [memory bus](../src/core/memory/memory.h). `registerMemoryDevice` installs a non-overlapping, inclusive 32-bit address range with read/write callbacks and an opaque context; `unregisterMemoryDevice` removes it by start address. The context is caller-owned and must remain valid until unregistration. This is a synchronous, single-threaded bus: each mapped access invokes exactly one callback with the original address and width, completing before the next guest operation. Ordinary memory loads/stores use the same bus, so read-clear, write-one-clear, FIFO, or other effects implemented by a device model are also visible to normal accesses. Successful reads and writes are masked to their access width. Tests exercise read-clear and write-one-clear models, ordering, widths, stack effects, and failure propagation.

Mapped ranges take precedence over backing RAM without modifying it. Overlapping registrations, unsupported access directions, and transfers straddling a range boundary (including address-space wraparound) are rejected. Callback errors propagate rather than falling back to RAM, including `READ_UNUSED_MEM`. Unregistered addresses retain the existing sparse-RAM behavior; device loads of uninitialized RAM still return the undefined-word marker for compatibility.

This implements the core device-instruction dispatch contract, not every peripheral on a particular chip. No built-in STi5518 peripheral model is registered by this change. Peripheral state, address-map configuration, and host callback pointers are not serialized in CPU or RAM snapshots; device owners must manage those separately.

Exact bus timings and hardware-specific alignment rules remain device-model responsibilities. `move` and `move2dall` batch aligned word transfers with exact byte prefixes/tails. Filtered 2D copies use word transfers for aligned, fully matching four-byte groups; mixed groups fall back to byte reads and selective writes. This does not provide native restartability or certify silicon timing or hardware-minimum transactions for differing alignments. Ordered `devmove` behavior is described below.

## Ordered device copy

`devmove` copies unsigned A bytes from C to B through the shared RAM/MMIO bus. It reads each four-byte chunk before its corresponding write, then transfers an exact one-, two-, or three-byte tail. Source read addresses and destination write addresses increase monotonically. The current bus supports widths 1-4, so this uses the minimum number of read/write transactions within that abstraction, without accessing bytes outside the requested ranges. Tests cover every source/destination alignment modulo four and lengths 0-17, including side-effecting MMIO transaction traces.

The reference explicitly excludes the on-chip peripheral range 0x20000000-0x3fffffff. Any nonempty source or destination span intersecting it is rejected before bus access, even if a device is registered there. External memory-mapped devices outside that range are supported. Zero-length copies perform no memory access. Overlapping or address-wrapping spans are rejected as an emulator diagnostic policy; this does not claim a hardware fault behavior for undefined inputs. A span ending at the last 32-bit address is accepted.

In native mode, read/write errors propagate immediately, with earlier completed chunks retained and no later accesses. A/B/C are undefined after the instruction, including failed or rejected copies. No rollback or restart state is created. The copy runs atomically at the instruction boundary with approximate timing, so native interruptible microcode restart remains unsupported. Portable mode instead retains resumable state and cached reads as described above. Unaligned word and three-byte tail transactions are abstract bus operations, not verified silicon access patterns; target-specific alignment/width constraints, device-region boundary errors, and bus errors propagate without retrying side-effecting accesses. These limits keep `devmove` partial.

## Semaphores, channels, and 2D moves

Semaphore records use `s.Count`, `s.Front`, and `s.Back` at word offsets 0, 1, and 2. Waiting lists store process descriptors, including their priority bit; ready queues use workspace pointers. A zero-count wait blocks, and signal releases the oldest waiter without adding a permit. With no waiter, signal increments the 32-bit count without overflow checking. An enabled Signal trap detaches the waiter and passes its descriptor to the kernel, which owns the enqueue decision.

Software channels use a channel word containing `NotProcess.p` or the waiting descriptor, with the buffer pointer saved at `pw.Pointer`. Both arrival orders are supported. Zero-length messages still synchronize without accessing message buffers. The caller must provide opposite communication directions and matching message lengths, as required by the manual; these preconditions are not independently tracked. Buffers and channel/workspace bookkeeping must not alias. Scalar output uses `pw.Temp` while blocked; its value is unspecified after completion. `resetch` can initialize previously unused software-channel RAM and returns a cancelled waiter without automatically enqueueing it.

Native mode supports memory-backed software words and explicitly registered external FIFO endpoints. Unregistered peripheral addresses and the core-reserved span before `MemStart` remain conservatively rejected; this is not a complete hard-channel address map. Software and configured FIFO ALT notifications are supported in both modes. Normal communication traps follow data completion; software channels are also cleared. An ALT notification trap passes the waiting owner to the kernel without consuming the message. Pending software-channel/ALT state resides in guest memory; configured FIFO state is retained by the external-channel model and EXCH snapshots. Native FIFO transfers are atomic, while portable transfers can use the resumable record format.

## Trap delivery

The default C2 trap area follows the existing implementation and the TP2 memory-map convention: handler address `0x80000040 + priority*0x80 + group*0x20`, with the paired trapped-process record at handler address + 0x10. Each record has Enables, Status, Wptr, and Iptr at word offsets 0-3. Groups are breakpoint, arithmetic error, system operation, and scheduler.

Enabled native delivery saves the interrupted record and loads the handler's Status/Wptr/Iptr. It loads the handler's full global-enable field (bits 16-31), but ANDs trap-enable bits with the handler mask. Independent `gintdis 0`/`trapdis 0` probes inside handlers and after `tret` agree with ST's simulator 6.8 at both priorities, including the documented event bits 16-23 and upper global bits. The manuals' blanket wording about ANDing Enables conflicts with this measured split; the native emulator follows the simulator contract, not a claim of physical-silicon certification. Portable mode retains its prior full-word AND policy.

Trap return selects the group from the active trap frame, without requiring the installed handler Status to encode it in bits 18-19. Scheduler return priority is recorded in bits 16-17; native same-priority return restores the pre-trap values for those bits instead of leaking the temporary overlay. Native cross-priority return instead uses the captured bank's Status and control fields, including verified edits. Saved global enables and the trap bank's saved mask are restored. A low-priority timer trap is taken only at low priority: it waits while a high-priority process runs, and an idle high-priority side first drops to low priority (OS20 starts the woken task through the low-priority trapped-process record). A timer trap masked by the running scheduler handler stays set and is taken after its return, using the enables that the return restores (ST20-TP2 3.6.3), instead of bypassing the kernel through the ready queue. An interrupt taken while low priority is idle returns to that idle state with its registers (Wptr) kept, as OS20's following QueueEmpty trap records it. Scheduler traps carrying a descriptor write it to handler entry workspace word 0; this workspace-slot convention is firmware-derived (OS20 reads it with `ldl` after `ajw`), not verified silicon behavior. A/B/C are neither saved nor changed on trap entry, so the handler sees the trapped process's evaluation stack (ST20-TP2 datasheet 42-1674-04, section 3.6.3); OS20's scheduler kernel stores them for a later `restart`. Cross-priority scheduler delivery preserves the calling register bank. Missing/null or unaligned handler workspaces, same-group reentry, malformed returns, and handler attempts to block are reported explicitly. Different groups may nest; asynchronous timer requests wait for active handlers to return.

The engine handles breakpoint, arithmetic overflow, truly illegal opcodes, monitored descriptor access, timer, run/start, signal, internal-channel, priority-interrupt, and queue-empty sources. `causeerror` can generate all fourteen enabled cause bits. Legal-but-unimplemented opcodes are not mislabeled as illegal. Disabled arithmetic traps set the current error flag; halt-on-error latches a processor halt and prevents fetch, execution, or scheduling from replacing the fault context.

Scheduler kernels receive the released/scheduled descriptor in B and must perform scheduling themselves, normally with the corresponding group masked. Same-priority `tret` restores saved control, not A/B/C; handler code must preserve those when needed. Native cross-priority return resumes the captured bank, including verified control and register edits. Return-bank redirection and native interrupted-operation reconstruction remain unsupported. Ordinary native `restart` restores a record at the current priority without enqueueing it. Portable `restart` additionally restores validated tagged records with pending operations, as described above.

Source qualifications are explicit: the JSON's `tret` formula names the handler record, but its description and both supplied datasheets describe restoring the trapped-process record; this implementation follows the latter. For `ldtrapped`/`sttrapped`, group/priority selectors follow the primary C2/C4 instruction reference, while the datasheet summaries describe current-priority access. These conventions and scheduler cross-priority behavior still need target-revision conformance testing; this is not a blanket claim of silicon equivalence.

Each 2D copy requires `move2dinit` beforehand. Strides are byte offsets; address arithmetic wraps at 32 bits, including backward strides. Zero rows or zero row width perform no memory accesses. Filtered copies leave skipped destination bytes untouched. Source and destination areas must not overlap; overlapping effects are undefined in the manual and are not diagnosed. Native-mode copies run atomically at the emulator instruction boundary. `move2dall` uses aligned word transfers with exact byte prefixes/tails per row; differing alignments and filtered copies use byte accesses. These paths do not certify the hardware's minimum-word-transfer microcode, native restartability, MMIO side-effect equivalence, or exact timing. Portable mode supports resumable 2D copies as described above.

## Alternative sequences

ALT state is held in guest workspace: pw.State at -3, pw.TLink at -4, pw.Time at -5, and pw.Temp at 0. Enabling, Waiting, and Ready are reserved state markers, not normal buffer pointers. Wait instructions initialize pw.Temp to NoneSelected before any process switch. A notification while enabling marks Ready without enqueueing; a notification while waiting wakes once. A channel sender remains blocked and its descriptor stays in the channel until the selected branch inputs; selection does not consume the message.

Selection follows disabling-instruction order, not arrival order. The first ready guard stores its branch offset, later guards cannot overwrite it, and unready self-owned channel registrations are cleared. Timer enabling retains the earliest target; waiting uses target + 1 for strict readiness. Expiry records observation time and TimeSet, without enqueueing an ALT already made Ready by a channel. `dist` removes outstanding timer membership even for a false guard, preserving unrelated links and alarms.

State and registrations survive snapshots through guest memory and existing timer/process metadata; no host-only ALT map is used for software channels. Code must follow a valid enable/wait/disable/end protocol, use input-channel guards, and avoid the reserved NoneSelected branch offset. Source interpretations remain labeled: `diss` follows its first-ready-guard description, not the reversed printed formula; `taltwt` uses the TP2 datasheet's `25 F1` while the JSON remains unchanged. Configured FIFO guards and their serialized model state work in both ABI modes. Physical external guard hardware and native interruptible microcode restart remain unverified.