# ST20 instruction implementation status

Updated: 2026-10-10.

Instruction list: [ST20 C2/C4 reference manual JSON](../manuals/ST20/ST20_C2_C4-Core-Instruction-Set-Reference-Manual--72-TRN-273-01.json).
Implementation: [instruction handlers](../src/core/cpu/instructions.cpp), [dispatch table](../src/core/cpu/instrentry.cpp), and [CPU helpers and decoder](../src/core/cpu/st20.cpp).

## Status definitions

- **Implemented**: the core operation has an active implementation. This does not certify complete hardware fidelity or every error/device integration path.
- **Partial**: an active implementation exists, but known instruction behavior is missing or limited.
- **Stub**: the handler changes stack state or prints a message without implementing the instruction's main effect.
- **Not implemented**: dispatch uses `invalidOp_`, or no usable encoding has been established. Independently corroborated encodings are noted when the primary reference is defective.

The 171 reference instructions comprise **149 implemented**, **21 partial**, **0 stubs**, and **1 not implemented**. Encoding components are listed separately below. The only instruction without a handler is reboot, deliberately excluded from this implementation. Partial includes functional portable-mode behavior whose silicon-specific conventions remain unverified.

Instruction timing remains approximate. Newly added handlers use a baseline cycle count of one, not a hardware-verified timing value. Undefined bit-reversal widths and invalid `xword` inputs use the existing undefined-word marker, 0xcccccccc; this is an emulator policy, not a claim about silicon-specific undefined behavior.

## Portable emulator ABI

The user explicitly authorized an emulator-specific ABI for undocumented formats. Enable it with `PORTABLE_INSTRUCTION_ABI = 0x1` in st20emu.ini, or `setPortableInstructionAbi(true)` for an embedded core. It is off by default. **These conventions are not asserted to match ST20 silicon and can break existing firmware.** The reference/native limitations described later in this document remain applicable when portable mode is disabled, except for the current snapshot format.

### Saved state and continuations

Portable records contain twelve 32-bit words. Word 0 transfers enable bits 16-23 and trap bits 0-13, preserving other destination bits on stshadow. Word 1 is Status; words 2-3 are Wptr/Iptr; words 4-6 are A/B/C. The five block words are 7=row width, 8=rows remaining, 9=destination stride, 10=source stride, 11=cached read word. For pending channel copies, words 9-10 instead hold channel address and peer descriptor; NotProcess.p as the peer identifies an external FIFO endpoint.

Status bit 25 tags the portable format. Bit 24 transports the private intdis flag. Bit 21 marks a successfully read but not yet committed word; bits 22-23 encode its width minus one. Bits 26-30 identify pending operations: 1=move, 2=devmove, 3=move2dall, 4=move2dzero, 5=move2dnonzero, 6=input copy, 7=output copy. A/B/C hold row bytes remaining, destination cursor, and source cursor while a copy is pending. These values become undefined on completion. Other native microcode operation codes are not accepted as portable continuations.

Copies checkpoint after committed transactions and can be preempted by high-priority work or queued IRQs. A cached successful read is retained across write faults, restart, and snapshots, so retrying does not repeat that MMIO read. Filtered 2D copies use byte transactions; other copies use up to four bytes per transaction. Abstract bus cycle accounting remains approximate. Short ALT/timer/semaphore bookkeeping is an atomic portable transaction: its guest workspace state and next Iptr provide continuation after blocking, not an invented replica of native internal microcode phases.

Portable ldshadow/stshadow support all four selector groups, captured trap callers, inactive high/low banks, and block-only current-bank loads. Edits to captured caller control state also update the paired trapped record. Current-bank context loads remain rejected because the reference declares them undefined. Portable restart requires a tagged record, validates the complete candidate before changing CPU state, restores the current priority's context/masks, and abandons the active trap stack. It does not implicitly enqueue the replaced process. Silicon restart layouts and return-bank conventions remain unverified.

### Floating-point conventions

For postnormsn, A/B contain the norm result's low/high significand words and C its shift count (0-64). pw.Temp holds sign in bit 31 and a signed 31-bit, unbiased pre-normalization exponent in bits 0-30. The helper subtracts C from that exponent, preserving sign, and leaves the normalized significand in A/B. A zero significand preserves signed zero. Exponents outside the representable portable range are clipped.

For roundsn, B:A is a normalized unsigned 64-bit significand with its leading bit at bit 63, or zero; C holds the same sign/signed-unbiased-exponent encoding. Its mathematical magnitude is `(B:A / 2^63) * 2^exponent`. Integer nearest-even rounding packs IEEE single precision into A, including gradual underflow, signed zero, carry into the next exponent, and overflow to infinity. B/C become undefined. Non-normalized nonzero inputs are rejected. NaN/infinity classification and special arithmetic remain caller responsibilities. This is a tested portable numerical contract, not a recovered hardware postnormsn/roundsn implementation.

### Hardware event models

`queueExternalInterrupt(level, workspace, iptr, status)` queues one of eight portable levels (7 highest). Delivery occurs at instruction or copy checkpoints, observes the appropriate global external-event mask, and nests only at a higher level. The supplied workspace is the save-frame base, not a guessed chip interrupt-vector register. Managed iret restores high, low-shadow, idle, or pending-copy context and the controller stack. Interrupt handlers cannot deschedule or perform channel communication/block moves. The register-level STi5518 interrupt controller is not emulated by these APIs.

Standalone startup additionally installs a separate native STi5518 ASC1/ASC2/controller model. ASC1 (`0x20004000`, source 9) and ASC2 (`0x20005000`, source 8) have independent registers, transmit delays and captures. Their readiness is routed through firmware-programmed source/level masks, trigger modes and vector registers; sources sharing a level are combined before evaluating its trigger. `serviceNativeInterrupt` loads handler Status/Iptr from HandlerWptr-8/-4 and uses the fixed save-area base HandlerWptr-32; a low-priority caller is held in shadow registers, while a high-priority caller uses the six-word save frame. Vector bit 0 controls priority relative to high-priority processes, global external-event masks and low-priority interruption disabling defer entry, and iret acknowledges the controller's executing level. Interrupted native operations remain unsupported. Transmit bytes are captured by `takeSti5518Asc1Transmit` and `takeSti5518Asc2Transmit`; readiness uses a short instruction-boundary delay rather than baud-accurate timing. Incoming data, full FIFO operation, unrelated peripherals and chip-state snapshots are not implemented. Peripheral polling and interrupt-return callbacks are owned by this board model while installed.

`registerExternalChannel(address, capacity)` installs a bounded FIFO endpoint; `feedExternalChannel` supplies incoming bytes and `drainExternalChannel` consumes outgoing bytes. Empty input and insufficient output capacity block through guest workspace state, then wake once through the FIFO or ExternalChannel trap kernel. ALT enables/disables and readiness notifications work with these endpoints. resetch clears FIFO/registration state and returns a canceled waiting descriptor without automatically enqueueing it. A channel permits one active transaction or waiter; conflicting callers are rejected. Resetting a channel during an active transfer cancels its model state and may make that continuation fail. Endpoint addresses are explicit model configuration, not a claimed hardware address map.

### Snapshots and limits

Version-11 snapshots use 32-bit CPU/register words in the prefix and a 550-word fixed runtime extension: timing data, cached move fields, inactive high state, eight pending IRQ requests and up to eight IRQ frames. An optional EXCH extension serializes FIFO bytes, capacities, active/waiting descriptors, and ALT notification state. Limits are 1024 endpoints, 1 MiB capacity per endpoint, and 16 MiB total queued data per snapshot; malformed state is validated before restoring the live machine. Only version 11 is supported; unversioned files, all other versions (including version 10), and incomplete runtime blocks are rejected without changing live state. Portable ABI configuration and arbitrary MMIO callback objects are board configuration and are not serialized. Guest RAM still must be saved/restored separately; native byte order, boolean representation, and structure padding keep files host-ABI dependent.

Native five-word register interpretation, native FP intermediate layout, exact bus/timing behavior, chip-specific event registers, and interrupted native operation codes remain unverified. They are not hidden by the portable implementation. reboot is untouched.

## Instructions

Limitations in the table describe native mode unless explicitly labeled portable.
Portable-mode continuations and event models are specified above; a Partial label
does not mean those portable implementations are absent.

The **st20sim verification** column records the 2026-10-10 differential run against
ST's simulator version 6.8: two rounds, seeds 2147012/2147013, 49 planned
cases per selected instruction per round. The complete sweep finished 15,876 cases
and 933,790 defined-value comparisons with no remaining divergences in the tested
scopes. The 588 planned cases in blocked batches are not counted as completed.

- **Passed**: all tested architecturally defined register, workspace readback and error-result checkpoints agreed within the fixture scope below.
- **Diverged**: at least one tested checkpoint disagreed; this does not establish which model matches silicon.
- **Blocked**: a fixture was attempted but one engine could not complete it; no equivalence claim is made.
- **Indirect**: exercised by setup, workspace readback or instruction encoding, not isolated test cases.
- **Not tested**: not covered by this differential run.

Of the 171 instructions, **162 passed**, **0 diverged**, **6 were blocked** and
**3 were exercised indirectly**; none remain unattempted. A pass is not complete
instruction or silicon certification. Native mode is used throughout. The driver
propagates reference-defined register and RAM definedness, excluding undefined
outputs even when the simulator prints concrete stale values. Simulator undefined
markers are also excluded. Timing and peripheral fidelity are not verified.

Set `ST20SIM_HOST` and `ST20SIM_ROOT` (the Windows toolset path) explicitly and
authenticate SSH in the terminal before running the tests. The
[differential driver](../tests/st20sim_differential.js) defaults to isolated
instruction batches, records fixture scopes and retains ROMs, manifests, traces
and reports under `obj/tests/st20sim-*`. Separate HP and LP runs are required:

```bash
make test-st20sim
make test-st20sim ST20SIM_ARGS='--priority 1'
```

Exit codes are 0 for agreement, 1 for divergences and 2 for blocked/infrastructure
failures. Isolated batches continue after a blocker. `--isolate 0` retains the
faster combined mode for compatible operations. The full default HP sweep currently
returns 2 because it includes unresolved native operations; the LP controls passed.

### Fixture Scope

| Instruction family | Tested scope / exclusions |
| --- | --- |
| Arithmetic, stack, pointer and flag operations | Boundary and seeded operands; architecture-defined results only, not every legal input or trap path. |
| Loads, stores and device accesses | Initialized RAM, supported alignments and short offsets; no MMIO callbacks or peripheral side effects. |
| Native moves and 2D moves | Atomic RAM copies of 0-16 bytes, two rows/stride 20 for 2D, filtered writes and untouched-byte readback; no interrupted operations. |
| Clock operations | Register data and enable masks with automatic HP/LP ticks frozen in both engines; no cycle or clock-rate certification. |
| Queue operations | Initialized RAM queues and readback, no preemption; `timeslice` uses empty queues only. `stlf` is checked from an initialized LP caller: simulator 6.8 does not expose the same LP-front mutation in the HP fixture. Our C2 LP-bank update is retained for both caller priorities. |
| Semaphores | `wait` with nonzero count; `signal` with an empty waiter queue, including count wrap; no blocking/wakeup sequence. |
| Channels | Matched software peer, 0-16-byte transfers or scalar output, peer enqueue and reset; no unmatched blocking or external channels. |
| ALT | Ready/skip/channel guard paths and disabling order; no blocking wait or timer expiry; `altend` uses offset 0. |
| Timer lists | `swaptimer` front swap/timestamp read; `tin` with an already-expired deadline; no alarm expiry or blocking. |
| LP interruption controls | `intdis`/`intenb` stack behavior with no pending HP work; `settimeslice` boolean readback, not periodic slicing. LP entry uses a real `runp`/`stopp` handoff. |
| Control flow and process changes | Resolved branches, call/return frames and ready-process handoff; `lend` uses boundary/seeded word counts with zero branch distance; `endp` final branch/count 1 only. |
| Trap records and return | Four-word records for all groups/both priorities; breakpoint handler/`tret` return with aligned handler inputs; no nested traps or full monitor behavior. |
| Shadow and restart records | Inactive LP bank/selectors 0-15, idle native block group and valid restart without a pending operation; native interrupted-state layout is not verified. |
| Identification | `ldprodid` uses an isolated, identity-aligned local configuration and checks its full stack contract; the production STi5518 ID is unchanged. `lddevid` is locally tested as the documented product-ID alias, but the simulator prints `???` and ignores its positive encoding. |

### Blocked Verification

| Instruction | Observed blocker |
| --- | --- |
| `causeerror` | Enabled group-handler scenario leaves the planned execution path and reaches an unimplemented opcode in our core; fixture/core cause remains unresolved. |
| `iret` | Simulator accesses controller state at `0x1ffffffc` outside the standalone memory map; a controller-backed interrupt-entry fixture is needed. |
| `lddevid` | Simulator 6.8 prints `???` for `21 27 FC` and leaves A/B/C unchanged; it is not an oracle for this alias. The native alias remains implemented and locally regression-tested. |
| `postnormsn` | Simulator completes the zero probe, but the emulator rejects native mode with `PORTABLE_ABI_DISABLED`; native intermediate format remains unsupported. |
| `roundsn` | Simulator completes the zero probe, but the emulator rejects native mode with `PORTABLE_ABI_DISABLED`; native intermediate format remains unsupported. |
| `reboot` | Bounded simulator reset probe does not complete; the emulator deliberately excludes this instruction. |

| Instruction | Status | st20sim verification | Remaining work / notes |
| --- | --- | --- | --- |
| `adc` | Implemented | Passed | Checked signed 32-bit immediate addition with overflow trap/error fallback. |
| `add` | Implemented | Passed | Checked signed 32-bit addition with overflow trap/error fallback. |
| `ajw` | Implemented | Passed | Adjusts the real 32-bit workspace pointer with wrapping word-offset arithmetic. |
| `alt` | Implemented | Passed | Begins alternative enabling without changing the integer stack. |
| `altend` | Implemented | Passed | Branches by the selected workspace offset from the following instruction. |
| `altwt` | Implemented | Passed | Initializes selection state and blocks only if no guard is ready. |
| `and` | Implemented | Passed | - |
| `bcnt` | Implemented | Passed | - |
| `bitcnt` | Implemented | Passed | Counts all 32 bits and wraps the sum. |
| `bitrevnbits` | Implemented | Passed | Reverses the selected low 0-32 bits; clears the other result bits. |
| `bitrevword` | Implemented | Passed | Reverses all 32 bits, preserving B and C. |
| `bsub` | Implemented | Passed | - |
| `call` | Implemented | Passed | Saves the call frame in guest memory and adjusts Wptr without allocation bounds. |
| `causeerror` | Implemented | Blocked | Delivers enabled user-generated causes and records the causeerror Status flag. |
| `cb` | Implemented | Passed | Checks signed eight-bit range without changing the stack; IntegerError trap/error fallback. |
| `cbu` | Implemented | Passed | Checks unsigned eight-bit range without changing the stack; IntegerError trap/error fallback. |
| `ccnt1` | Implemented | Passed | Checks unsigned B in 1..A inclusive, pops the bound, and signals IntegerError on failure. |
| `cflerr` | Implemented | Passed | Detects IEEE single-precision infinity/NaN bit patterns, preserves the stack, and signals IntegerError. |
| `cir` | Implemented | Passed | Checks signed C in A..B inclusive, retaining C in A; IntegerError trap/error fallback. |
| `ciru` | Implemented | Passed | Checks unsigned C in A..B inclusive, retaining C in A; IntegerError trap/error fallback. |
| `cj` | Implemented | Passed | - |
| `clockdis` | Implemented | Passed | Clock-enable register update. |
| `clockenb` | Implemented | Passed | Clock-enable register update. |
| `clrhalterr` | Implemented | Passed | Clears the halt-on-error mode flag without changing stack or error flags. |
| `crcbyte` | Implemented | Passed | Updates the checksum from A's most significant byte using the polynomial in C. |
| `crcword` | Implemented | Passed | Updates the checksum from all 32 bits of A using the polynomial in C. |
| `cs` | Implemented | Passed | Checks signed sixteen-bit range without changing the stack; IntegerError trap/error fallback. |
| `csngl` | Implemented | Passed | Checks that B is A's signed extension word, retains A and consumes B (old C moves into B); IntegerError trap/error fallback. |
| `csu` | Implemented | Passed | Checks unsigned sixteen-bit range without changing the stack; IntegerError trap/error fallback. |
| `csub0` | Implemented | Passed | Checks unsigned B in 0..(A-1), pops the bound, and signals IntegerError on failure. |
| `cword` | Implemented | Passed | Checks signed B in -A..(A-1) using A's power-of-two sign-bit magnitude, including 0x80000000; pops the bound. |
| `devlb` | Implemented | Passed | Ordered unsigned byte load from RAM or a registered MMIO device; device side effects and errors supported. |
| `devls` | Implemented | Passed | Ordered unsigned sixteen-bit load from RAM or a registered MMIO device; one width-preserving device transaction. |
| `devlw` | Implemented | Passed | Ordered word load from RAM or a registered MMIO device; one width-preserving device transaction. |
| `devmove` | Partial | Passed | Ordered RAM/external-MMIO copying in four-byte chunks with exact tails; on-chip peripherals excluded. Interruptible restart and target-specific bus cycles remain unsupported. |
| `devsb` | Implemented | Passed | Ordered byte store to RAM or a registered MMIO device; masks B to eight bits and consumes two stack words. |
| `devss` | Implemented | Passed | Ordered sixteen-bit store to RAM or a registered MMIO device; masks B to sixteen bits and consumes two stack words. |
| `devsw` | Implemented | Passed | Ordered word store to RAM or a registered MMIO device; one width-preserving device transaction. |
| `diff` | Implemented | Passed | Unchecked 32-bit subtraction. |
| `disc` | Partial | Passed | Disables software-channel registration and selects the first ready guard; external guards unsupported. |
| `diss` | Implemented | Passed | First enabled skip guard selects, following the description rather than the reversed printed branch. |
| `dist` | Partial | Passed | Selects a ready timer and removes outstanding timer-list membership; microcode restart unsupported. |
| `div` | Implemented | Passed | Signed division, defined stack effects, overflow trap/error flag, and halt fallback. |
| `dup` | Implemented | Passed | - |
| `enbc` | Partial | Passed | Enables software-channel guards and detects waiting senders; external guards unsupported. |
| `enbs` | Implemented | Passed | Marks an enabled skip guard ready without changing registers. |
| `enbt` | Implemented | Passed | Records the earliest enabled timer guard with wrapping time comparisons. |
| `endp` | Implemented | Passed | Retires non-final parallel branches; the final branch enters the successor directly at the current priority. |
| `eqc` | Implemented | Passed | - |
| `fmul` | Implemented | Passed | Signed Q1.31 fractional multiplication with nearest-even rounding; -1 times -1 signals IntegerOverflow with an undefined-word result. |
| `fptesterr` | Implemented | Passed | Pushes true for the modeled CPU without an FPU; leaves integer error flags unchanged. |
| `gajw` | Implemented | Passed | Exchanges A and Wptr without overwriting workspace memory. |
| `gcall` | Implemented | Passed | - |
| `gintdis` | Implemented | Passed | Clears selected bits in the 16-bit global-enable field and returns its previous value; only the documented low eight event bits currently affect execution. |
| `gintenb` | Implemented | Passed | Sets selected bits in the 16-bit global-enable field and returns its previous value; only the documented low eight event bits currently affect execution. |
| `gt` | Implemented | Passed | Signed 32-bit comparison. |
| `gtu` | Implemented | Passed | Unsigned comparison. |
| `in` | Partial | Passed | Internal rendezvous, selected ALT input, and trap delivery implemented; external channels and microcode restart unsupported. |
| `insertqueue` | Implemented | Passed | Prepends the selected priority's list, joins its tail to the old front, and preserves the old back; empty input is a no-op. |
| `intdis` | Implemented | Passed | Suppresses high-priority process preemption and pending high-priority timer kernels until reenabling or descheduling; preserves the stack and global mask. |
| `intenb` | Implemented | Passed | Clears the low-priority interruption-disable state; pending high-priority work can run at the next scheduler boundary. |
| `iret` | Partial | Blocked | Restores saved high-priority state or low shadow/idle state and notifies a registered controller observer; interrupted microcode restart and full external IRQ integration remain unsupported. |
| `j` | Implemented | Passed | Relative jump and periodic timeslicing point; enabled `j 0` delivers a breakpoint trap. |
| `ladd` | Implemented | Passed | Checked signed carry-in addition with overflow delivery. |
| `lb` | Implemented | Passed | - |
| `lbx` | Implemented | Passed | - |
| `ldc` | Implemented | Indirect | - |
| `ldclock` | Implemented | Passed | Reads the explicitly selected clock register. |
| `lddevid` | Implemented | Blocked | Product-ID alias; decoder remap reaches its handler. |
| `ldiff` | Implemented | Passed | Unsigned difference and borrow. |
| `ldinf` | Implemented | Passed | Pushes the IEEE single-precision positive-infinity bit pattern, 0x7f800000. |
| `ldiv` | Implemented | Passed | Unsigned double-word division and overflow delivery; A/B remain unchanged on overflow. |
| `ldl` | Implemented | Indirect | Reads guest memory at a wrapping word offset from Wptr. |
| `ldlp` | Implemented | Passed | Computes a wrapping word-offset address without allocation bounds. |
| `ldmemstartval` | Implemented | Passed | Loads the configured memory-start value. |
| `ldnl` | Implemented | Passed | - |
| `ldnlp` | Implemented | Passed | - |
| `ldpi` | Implemented | Passed | - |
| `ldpri` | Implemented | Passed | Loads the explicit current-process priority. |
| `ldprodid` | Implemented | Passed | Loads the configured product ID. |
| `ldshadow` | Partial | Passed | Loads selected enables, ordinary low-shadow context, integer-stack groups, and idle native block records; interrupted native operations and inactive state banks inside traps remain unsupported. |
| `ldtimer` | Implemented | Passed | Selects the current process priority's clock independently of workspace bookkeeping. |
| `ldtraph` | Implemented | Passed | Installs four descriptor words or enters the enabled LoadTrap monitor without copying. |
| `ldtrapped` | Implemented | Passed | Installs four trapped-process words using the reference's group/priority selectors; LoadTrap monitoring supported. |
| `lend` | Implemented | Passed | Always decrements the wrapping word count; old signed count > 1 additionally increments index and branches backward from the next instruction. Follows the description and simulator rather than the printed terminal-count omission. |
| `lmul` | Implemented | Passed | Unsigned double-word product with addend. |
| `ls` | Implemented | Passed | - |
| `lshl` | Implemented | Passed | Defined counts supported; out-of-range result uses the undefined-word marker. |
| `lshr` | Implemented | Passed | Defined counts supported; out-of-range result uses the undefined-word marker. |
| `lsub` | Implemented | Passed | Checked signed borrow-in subtraction with overflow delivery. |
| `lsum` | Implemented | Passed | Unsigned sum and carry. |
| `lsx` | Implemented | Passed | Loads a sixteen-bit memory value and sign-extends it to 32 bits. |
| `mint` | Implemented | Passed | - |
| `move` | Partial | Passed | Byte copying present; interruptible/restartable transfer state is not modeled. |
| `move2dall` | Partial | Passed | Strided RAM copy with wrapping addresses; interruptible restart and exact bus/timing behavior unsupported. |
| `move2dinit` | Implemented | Passed | Sets row count and byte strides in process state, preserved by shadowing and snapshots. |
| `move2dnonzero` | Partial | Passed | Copies only nonzero bytes and preserves skipped destination bytes; interruptible restart and exact bus behavior unsupported. |
| `move2dzero` | Partial | Passed | Copies only zero bytes and preserves skipped destination bytes; interruptible restart and exact bus behavior unsupported. |
| `mul` | Implemented | Passed | Checked signed product with overflow delivery and safe host arithmetic. |
| `nop` | Implemented | Passed | - |
| `norm` | Implemented | Passed | Normalizes B:A as an unsigned double word; returns leading-zero count in C. |
| `not` | Implemented | Passed | - |
| `or` | Implemented | Passed | - |
| `out` | Partial | Passed | Internal rendezvous, ALT notification, and trap delivery implemented; external channels and microcode restart unsupported. |
| `outbyte` | Partial | Passed | Internal buffered byte output, ALT notification, and trap delivery implemented; hardware channels and restart unsupported. |
| `outword` | Partial | Passed | Internal buffered word output, ALT notification, and trap delivery implemented; hardware channels and restart unsupported. |
| `pop` | Implemented | Passed | - |
| `postnormsn` | Partial | Blocked | Opt-in portable exponent correction and signed-zero handling; native intermediate format remains unverified. |
| `prod` | Implemented | Passed | Unchecked wrapped product without host signed overflow. |
| `reboot` | Not implemented | Blocked | - |
| `rem` | Implemented | Passed | Signed B modulo A; division by zero signals overflow. Follows the description rather than the reversed printed formula. |
| `resetch` | Partial | Passed | Initializes/resets software channel words and returns the previous descriptor without rescheduling it; hardware reset unsupported. |
| `restart` | Partial | Passed | Restores native records with no interrupted operation, or complete tagged portable records including pending operations; native interrupted-operation format remains unverified. |
| `ret` | Implemented | Passed | Restores Iptr from the call frame and advances the real workspace pointer. |
| `rev` | Implemented | Passed | - |
| `roundsn` | Partial | Blocked | Opt-in portable integer nearest-even IEEE packing, gradual underflow and overflow; native intermediate format remains unverified. |
| `runp` | Implemented | Passed | Enqueues normally or passes the descriptor to the selected priority's Run trap kernel. |
| `satadd` | Implemented | Passed | Signed addition clipped to the 32-bit integer range, without overflow traps. |
| `satmul` | Implemented | Passed | Signed multiplication clipped to the 32-bit integer range, without overflow traps. |
| `satsub` | Implemented | Passed | Signed subtraction clipped to the 32-bit integer range, without overflow traps. |
| `saveh` | Implemented | Passed | Saves high-priority front/back pointers to two guest words and pops A; second-word address wraps at 32 bits. |
| `savel` | Implemented | Passed | Saves low-priority front/back pointers to two guest words and pops A; second-word address wraps at 32 bits. |
| `sb` | Implemented | Passed | - |
| `seterr` | Implemented | Passed | Sets the current priority's error flag and honors halt-on-error mode. |
| `sethalterr` | Implemented | Passed | Sets halt-on-error mode and halts if the current priority already has an error. |
| `settimeslice` | Implemented | Passed | Updates low-priority Status bit 20 from boolean A and returns the previous enable state; controls periodic slicing at j. |
| `shl` | Implemented | Passed | Defined counts supported; out-of-range result uses the undefined-word marker. |
| `shr` | Implemented | Passed | Defined counts supported; out-of-range result uses the undefined-word marker. |
| `signal` | Implemented | Passed | Unchecked permit increment or FIFO waiter release, with enabled Signal trap delivery. |
| `slmul` | Implemented | Passed | Signed A times signed B plus unsigned C; returns raw double-word high/low bits. |
| `ss` | Implemented | Passed | - |
| `ssub` | Implemented | Passed | - |
| `startp` | Implemented | Passed | Saves the new process entry address and enqueues it at the current priority. |
| `stclock` | Implemented | Passed | Writes the explicitly selected clock register. |
| `sthb` | Implemented | Passed | Sets the high-priority queue back pointer from A and pops A. |
| `sthf` | Implemented | Passed | Sets the high-priority queue front pointer from A and pops A. |
| `stl` | Implemented | Indirect | Writes guest memory at a wrapping word offset from Wptr. |
| `stlb` | Implemented | Passed | Sets the low-priority queue back pointer from A and pops A. |
| `stlf` | Implemented | Passed | Sets the low-priority queue front pointer from A and pops A; simulator verification requires an initialized LP caller. |
| `stnl` | Implemented | Passed | - |
| `stoperr` | Implemented | Passed | Saves Iptr and deschedules only when the current error flag is set. |
| `stopp` | Implemented | Passed | Saves Iptr and deschedules, dispatching a ready process or restoring low-priority shadow state. |
| `stshadow` | Partial | Passed | Stores selected enable/context/stack groups and idle native block records, preserving unselected words; native enable word is fully written, while portable mode preserves reserved destination bits. Native interrupted operations remain unsupported. |
| `sttimer` | Implemented | Passed | Initializes and enables both clocks and services due timer alarms and ready processes. |
| `sttraph` | Implemented | Passed | Exports four handler words or enters the enabled StoreTrap monitor without copying. |
| `sttrapped` | Implemented | Passed | Exports four trapped-process words; StoreTrap monitoring supported. |
| `sub` | Implemented | Passed | Checked signed subtraction with overflow delivery. |
| `sulmul` | Implemented | Passed | Signed A times unsigned B plus unsigned C; returns raw double-word high/low bits. |
| `sum` | Implemented | Passed | - |
| `swapqueue` | Implemented | Passed | Exchanges the selected priority's queue with B/C, returning the old front/back in A/B. |
| `swaptimer` | Implemented | Passed | Exchanges the selected timer-list front and updates its alarm from the new list. |
| `talt` | Implemented | Passed | Initializes alternative state and the TimeNotSet marker. |
| `taltwt` | Partial | Passed | Waits for channels/skip/timers and installs the earliest deadline; restart unsupported. Uses TP2-corroborated `25 F1` despite printed `25 FI`. |
| `testerr` | Implemented | Passed | Pushes inverted error state and clears the flag at the current priority; old A/B move into B/C. |
| `testhalterr` | Implemented | Passed | Pushes the halt-on-error mode as a boolean, shifting the stack. |
| `testpranal` | Implemented | Passed | Pushes the latched analyse-on-reset input; ordinary resets latch false, explicit analysed resets latch true. |
| `timeslice` | Implemented | Passed | Explicit FIFO rotation at either priority, or scheduler trap delivery with caller descriptor in B; unaffected by process disable controls. |
| `tin` | Partial | Passed | Timer waits, idle advancement, wakeups, and timer-kernel delivery implemented; interruptible microcode restart unsupported. |
| `trapdis` | Implemented | Passed | Changes the selected priority's mask and returns its previous value. |
| `trapenb` | Implemented | Passed | Changes the selected priority's mask and returns its previous value. |
| `tret` | Implemented | Passed | Restores the saved trapped-process record, masks, Status, Wptr, Iptr, and original return bank. |
| `unpacksn` | Implemented | Passed | Returns a left-aligned significand including the normal hidden bit; denormals use exponent 1, non-finite significands are undefined, and C holds wrapped 4*old-B plus the type code. |
| `wait` | Implemented | Passed | Consumes a permit or appends the current descriptor to the semaphore FIFO and suspends. |
| `wcnt` | Implemented | Passed | Converts a signed byte offset to its signed word offset and low two-bit byte selector. |
| `wsub` | Implemented | Passed | - |
| `wsubdb` | Implemented | Passed | - |
| `xbword` | Implemented | Passed | Sign-extends the least significant byte, clearing stale upper bits. |
| `xdble` | Implemented | Passed | Signed double-word extension. |
| `xor` | Implemented | Passed | - |
| `xsword` | Implemented | Passed | Sixteen-bit sign extension, including clearing stale upper bits. |
| `xword` | Implemented | Passed | Sign-extends B using the power-of-two sign-bit mask in A. |

## Encoding components

These three components are handled inside `decodeNextInstr()`, not by standalone instruction handlers. They are additional to the 171 entries above.

| Component | Status | st20sim verification | Implementation |
| --- | --- | --- | --- |
| `pfix` | Implemented | Indirect | Accumulates positive operand prefixes in the decoder. |
| `nfix` | Implemented | Indirect | Complements the accumulated operand for a negative prefix. |
| `opr` | Implemented | Indirect | Decodes the secondary operand and selects its dispatch entry, including the `lddevid` remap. Unsupported secondary instructions remain unimplemented. |

## Timer and process model

The HP clock runs at 1 MHz and the LP clock at 15,625 Hz against the configured `CPU_CLOCK`. Fractional phases retain remainders modulo `CPU_CLOCK`; elapsed-cycle batches generate every tick and clock values wrap at 32 bits. Disabled clocks preserve both value and phase. CPU initialization resets phases; clock-register writes preserve divider phase.

Processes have explicit Wptr and priority fields. `nWptr` is legacy/debug bookkeeping, not a hardware address bound. Ready queues are FIFO within each priority. High-priority dispatch preserves the interrupted low-priority integer registers, Iptr, Wptr, and priority. Preemption occurs after the current instruction completes. When no process is runnable, decoding advances virtual time to the next enabled timer alarm; if none can run, it returns `CPU_IDLE` to the CLI rather than executing stale code.

Timer deadlines use wrapping comparisons within half of the 32-bit counter range. `tin` waits until the clock is strictly after the requested time by scheduling at `time + 1`. Timer and process links reside in guest workspace memory. Enabled timer/run/signal/internal-channel events enter scheduler trap kernels instead of automatically enqueueing the released process; B carries the process descriptor. Software-channel/skip/timer alternatives and periodic timeslicing at j are supported. Native hardware event integration and native interruptible microcode restart remain incomplete; portable mode supports configured external FIFO guards and queued interrupts as described above.

Current snapshots use version 11 as specified in the portable ABI section, using 32-bit CPU/register words in the prefix. Older files are rejected; all fixed runtime fields are required. Guest memory, including trap records, must still be saved/restored separately. Snapshot files remain host-layout dependent.

## Selective shadow transfers

This section describes native-mode transfers. Portable mode additionally supports
the tagged twelve-word records and selector groups described above.

`ldshadow` and `stshadow` use A as a selector, B as priority 0/1, and C as a guest record address. Native selector bit 0 transfers the full global-enable field (bits 16-31) and the selected priority's trap enables (bits 0-13) in record word 0. Portable mode retains its documented eight-bit global field (bits 16-23). Bit 1 transfers Status/Wptr/Iptr in words 1-3; bit 2 transfers A/B/C in words 4-6. Successful instructions leave the current integer stack undefined. Addresses wrap at 32 bits. Empty selections perform no memory access; selector bits above bit 3 are ignored.

Enable-group transfers work in either direction between priorities without replacing the active bank's trap mask. Ordinary state-group loads support the inactive low-priority bank from high priority; stores also support the current bank. Loads read and validate every selected word before committing masks or shadow state. Stores preserve unselected words. Native word 0 is written completely without a destination read; bits 14-15 are zero. Portable stores retain their read/modify/write reserved-bit preservation and uninitialized-word policy. Native invalid-bank exports retain control status with bit 31 cleared. Mapped-device errors propagate. Store failures may leave earlier destination words written, but do not consume the caller's argument registers.

The engine tracks process validity separately from raw Status, so exported Status bit 31 is derived from running/shadow-valid metadata. Loading bit 31 activates or invalidates the low shadow; a context loaded as valid must have an aligned Wptr. An absent inactive bank exports invalid Status, `NotProcess.p` as Wptr, and undefined register values. Stack and context groups can be loaded separately before activation. Updated low shadows and enable masks survive current version-11 snapshots.

Selector bit 3 transfers five block-move words (7-11) for native contexts with no interrupted operation. Native moves are atomic at instruction boundaries: stores export the undefined-word marker for these dormant microcode registers, and loads read every selected word without changing live move configuration. Block-only transfers are also available for the current bank during traps. The silicon encoding of active block registers is not implemented: selected banks with an interrupted operation and interrupted-operation Status imports return `INTERRUPTED_OPERATION_UNSUPPORTED`. Current-bank context loads, high-priority state access from low priority, and inactive ordinary state-bank accesses during active traps remain rejected. Enable-only accesses remain available during traps. Abortable instruction microcode and external shadow-record transport of the engine's private interruption-disable flag are not implemented. These restrictions keep both instructions partial.

Native `restart` reads all twelve record words before committing, restores enables and Status/Wptr/Iptr/A/B/C at the current priority, and replaces the current process without enqueueing it. It supports only records with no interrupted operation and validates alignment for valid contexts. Successful replacement abandons the trap stack and rearms queue-empty notification for a running process. The five dormant block words are consumed but not interpreted; move configuration and private interruption state start cleared. Native interrupted-operation restart remains unsupported, including preservation of preconfigured 2D move parameters through these guest records. Portable mode retains its separate tagged continuation layout.

## External interrupt return

This section describes native-mode save areas. Portable managed IRQ frames can
also restore pending copies and controller nesting, as described above.

`iret` reads the external-interrupt save area at Wptr. With Status bit 31 (valid) set, its six consecutive words hold Status, Wptr, Iptr, A, B, and C of the interrupted high-priority process. These words are read and validated before changing the active context. A null/invalid Status restores the complete interrupted low-priority shadow if one exists; otherwise it returns to idle rather than executing stale handler code. The low shadow is consumed exactly once and survives CPU snapshots.

The primary instruction reference prints the frame addresses as `Wptr + 1` through `Wptr + 5`. The implementation uses word offsets, corroborated by the six-word save area in Figure 4.2 of the [TP2 datasheet](../manuals/ST20-TP2/ST20-TP20-Datasheet--42-1674-04.pdf), not overlapping byte-offset reads. That datasheet also specifies the null-Status case for an interrupted idle CPU. The source JSON remains unchanged.

`setInterruptReturnObserver` registers an optional synchronous controller notification. It runs once after a successful return and sees the restored context; malformed frames, memory-read failures, and unsupported returns do not notify it. Callback configuration and external controller state are not serialized. With no observer, CPU restoration still works but no physical controller is acknowledged. This does not add external interrupt entry, vector dispatch, or controller priority/nesting logic.

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

This implements the core device-instruction dispatch contract, not every peripheral on a particular chip. No built-in STi5518 peripheral model is registered by this change. Peripheral state, address-map configuration, and host callback pointers are not serialized in CPU or RAM snapshots; device owners must manage those separately. Exact bus timings and hardware-specific alignment rules remain device-model responsibilities. `move` and the 2D moves still use byte transfers and do not gain hardware-minimum word accesses or restartability from this bus support. Ordered `devmove` behavior is described below.

## Ordered device copy

`devmove` copies unsigned A bytes from C to B through the shared RAM/MMIO bus. It reads each four-byte chunk before its corresponding write, then transfers an exact one-, two-, or three-byte tail. Source read addresses and destination write addresses increase monotonically. The current bus supports widths 1-4, so this uses the minimum number of read/write transactions within that abstraction, without accessing bytes outside the requested ranges. Tests cover every source/destination alignment modulo four and lengths 0-17, including side-effecting MMIO transaction traces.

The reference explicitly excludes the on-chip peripheral range 0x20000000-0x3fffffff. Any nonempty source or destination span intersecting it is rejected before bus access, even if a device is registered there. External memory-mapped devices outside that range are supported. Zero-length copies perform no memory access. Overlapping or address-wrapping spans are rejected as an emulator diagnostic policy; this does not claim a hardware fault behavior for undefined inputs. A span ending at the last 32-bit address is accepted.

In native mode, read/write errors propagate immediately, with earlier completed chunks retained and no later accesses. A/B/C are undefined after the instruction, including failed or rejected copies. No rollback or restart state is created. The copy runs atomically at the instruction boundary with approximate timing, so native interruptible microcode restart remains unsupported. Portable mode instead retains resumable state and cached reads as described above. Unaligned word and three-byte tail transactions are abstract bus operations, not verified silicon access patterns; target-specific alignment/width constraints, device-region boundary errors, and bus errors propagate without retrying side-effecting accesses. These limits keep `devmove` partial.

## Semaphores, channels, and 2D moves

Semaphore records use `s.Count`, `s.Front`, and `s.Back` at word offsets 0, 1, and 2. Waiting lists store process descriptors, including their priority bit; ready queues use workspace pointers. A zero-count wait blocks, and signal releases the oldest waiter without adding a permit. With no waiter, signal increments the 32-bit count without overflow checking. An enabled Signal trap detaches the waiter and passes its descriptor to the kernel, which owns the enqueue decision.

Software channels use a channel word containing `NotProcess.p` or the waiting descriptor, with the buffer pointer saved at `pw.Pointer`. Both arrival orders are supported. Zero-length messages still synchronize without accessing message buffers. The caller must provide opposite communication directions and matching message lengths, as required by the manual; these preconditions are not independently tracked. Buffers and channel/workspace bookkeeping must not alias. Scalar output uses `pw.Temp` while blocked; its value is unspecified after completion. `resetch` can initialize previously unused software-channel RAM and returns a cancelled waiter without automatically enqueueing it.

Native-mode channel support is deliberately limited to memory-backed software words. Known peripheral addresses and the configured core-reserved span before `MemStart` are conservatively rejected; this is not a complete hard-channel address map. Software-channel ALT notification is supported; native hardware endpoints and guards are not. Portable mode additionally supports configured external FIFO endpoints and their ALT guards. Normal communication traps follow copying and channel clearing; an ALT notification trap passes the waiting owner to the kernel without consuming the message. Pending channel/ALT state resides in guest memory, with portable endpoint state also retained by the external-channel model.

## Trap delivery

The default C2 trap area follows the existing implementation and the TP2 memory-map convention: handler address `0x80000040 + priority*0x80 + group*0x20`, with the paired trapped-process record at handler address + 0x10. Each record has Enables, Status, Wptr, and Iptr at word offsets 0-3. Groups are breakpoint, arithmetic error, system operation, and scheduler.

Enabled delivery saves the interrupted record, loads the handler's Status/Wptr/Iptr, and ANDs its enable mask. Trap return selects the group from the active trap frame, without requiring the installed handler Status to encode it in bits 18-19; scheduler return priority is recorded in bits 16-17. Scheduler traps carrying a descriptor also write it to handler entry workspace word 0 and retain it in Breg. This workspace-slot convention is firmware-derived compatibility behavior, not verified silicon behavior. A/B/C are not automatically saved for same-priority handlers. Cross-priority scheduler delivery preserves the calling register bank. Missing/null or unaligned handler workspaces, same-group reentry, malformed returns, and handler attempts to block are reported explicitly. Different groups may nest; asynchronous timer requests wait for active handlers to return.

The engine handles breakpoint, arithmetic overflow, truly illegal opcodes, monitored descriptor access, timer, run/start, signal, internal-channel, priority-interrupt, and queue-empty sources. `causeerror` can generate all fourteen enabled cause bits. Legal-but-unimplemented opcodes are not mislabeled as illegal. Disabled arithmetic traps set the current error flag; halt-on-error latches a processor halt and prevents fetch, execution, or scheduling from replacing the fault context.

Scheduler kernels receive the released/scheduled descriptor in B and must perform scheduling themselves, normally with the corresponding group masked. `tret` restores the saved process, not A/B/C; handler code must preserve those when needed. Return-priority redirection through an edited saved record is rejected; native `restart` and native interruptible-operation reconstruction remain unsupported. Portable `restart` restores validated tagged records, including pending operations, as described above.

Source qualifications are explicit: the JSON's `tret` formula names the handler record, but its description and both supplied datasheets describe restoring the trapped-process record; this implementation follows the latter. For `ldtrapped`/`sttrapped`, group/priority selectors follow the primary C2/C4 instruction reference, while the datasheet summaries describe current-priority access. These conventions and scheduler cross-priority behavior still need target-revision conformance testing; this is not a blanket claim of silicon equivalence.

Each 2D copy requires `move2dinit` beforehand. Strides are byte offsets; address arithmetic wraps at 32 bits, including backward strides. Zero rows or zero row width perform no memory accesses. Filtered copies leave skipped destination bytes untouched. Source and destination areas must not overlap; overlapping effects are undefined in the manual and are not diagnosed. Native-mode copies run atomically at the emulator instruction boundary and use byte accesses, not the hardware's minimum-word-transfer microcode. They implement normal RAM data effects without claiming native restartability, MMIO side-effect equivalence, or exact transfer timing. Portable mode supports resumable 2D copies as described above.

## Alternative sequences

ALT state is held in guest workspace: pw.State at -3, pw.TLink at -4, pw.Time at -5, and pw.Temp at 0. Enabling, Waiting, and Ready are reserved state markers, not normal buffer pointers. Wait instructions initialize pw.Temp to NoneSelected before any process switch. A notification while enabling marks Ready without enqueueing; a notification while waiting wakes once. A channel sender remains blocked and its descriptor stays in the channel until the selected branch inputs; selection does not consume the message.

Selection follows disabling-instruction order, not arrival order. The first ready guard stores its branch offset, later guards cannot overwrite it, and unready self-owned channel registrations are cleared. Timer enabling retains the earliest target; waiting uses target + 1 for strict readiness. Expiry records observation time and TimeSet, without enqueueing an ALT already made Ready by a channel. `dist` removes outstanding timer membership even for a false guard, preserving unrelated links and alarms.

State and registrations survive snapshots through guest memory and existing timer/process metadata; no host-only ALT map is used for software channels. Code must follow a valid enable/wait/disable/end protocol, use input-channel guards, and avoid the reserved NoneSelected branch offset. Source interpretations remain labeled: `diss` follows its first-ready-guard description, not the reversed printed formula; `taltwt` uses the TP2 datasheet's `25 F1` while the JSON remains unchanged. Native external guard hardware and native interruptible microcode restart remain unsupported; portable FIFO guards and their serialized model state are described above.