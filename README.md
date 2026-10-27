# ST20Emu

ST20Emu is an emulator for the ST20, a 32-bit transputer-based processor architecture developed by SGS-Thomson (later STMicroelectronics). Widely used throughout the late 1990s and early 2000s, ST20 cores powered many iconic digital Set-Top Boxes (STBs) and DVD players of the era.

## Development

### Requirements

- clang 18.1.3
- make

### Building

Build a debug version:

```bash
make clean && make
```

Build an optimized release version (`-O3`, link-time optimization, no sanitizers) as
`st20emu-release`, with its objects in `obj/release`, so it doesn't overwrite the debug build:

```bash
make release
```

Use the release build for long firmware runs; the sanitized debug build is many times slower.

Run the CPU instruction tests:

```bash
make test
```

Alternatively, with Meson and Ninja installed:

```bash
meson setup builddir
meson compile -C builddir
meson test -C builddir --print-errorlogs
```

The Meson executable is `builddir/st20emu`. With `just` installed, `just build`
and `just test` run the same Meson workflow.

## Getting started

Run the binary from your terminal:

```bash
./st20emu
```

For unattended firmware execution:

```bash
./st20emu --firmware firmware/6300.bin --batch
```

Batch mode runs CPU instructions until the first fetch or execution error (including
CPU halt or idle), then exits with status `1`. With SoC peripherals installed, an idle CPU lets
time pass in 1000-cycle steps (up to 10 emulated seconds, or until the next timer alarm) so
that peripheral interrupts can wake it, as on silicon. It never reads keyboard input or
opens a prompt, and ignores watches and the interactive instruction-count limits.
Firmware-loading failures also exit with status `1`; invalid arguments exit with
status `2`. Firmware that never encounters an error runs indefinitely.

The terminal prints the log filename. Both stdout and stderr, including startup
diagnostics, are saved in the current working directory as
`st20emu-log-YYYYMMDD-HHMMSS.txt`. During execution, the instruction count, decoded
instruction, CPU state, and diagnostics are logged for every instruction. Output
is captured as immutable register, instruction-byte, and workspace snapshots;
a background worker formats the trace and writes it to disk. Snapshots are handed
off every 100,000 instructions, with no byte-size threshold. Up to two batches
can wait behind the batch being written, bounding the number of queued records;
memory usage depends on the captured workspace and diagnostics. Execution waits
for the worker only when this queue fills.
Capturing snapshots and formatting execution diagnostics still take CPU time;
lossless full tracing cannot outrun the logger indefinitely. Any remaining output, including
the final error, CPU state, and total instruction count, is drained and flushed
when execution stops. Forced termination can lose buffered or queued output.
If a log with the same timestamp already exists, startup fails without overwriting it.

By default the image is placed immediately below `0x80000000`, as with `l filename`,
and execution starts at the configured `START_ADDR` (default `0x7ffffffe`). To load
and start at an explicit nonzero hexadecimal address:

```bash
./st20emu --firmware firmware/program.bin --address 0x1000 --batch
```

Omit `--batch` to load the firmware at startup and continue interactively.
Use `--help` for available options. An address of zero selects default placement.

Once inside the interactive command prompt (`>`), load a binary file containing valid ST20 instructions (such as a TSOP dump) using the `l` command:

```plaintext
l firmware/6300.bin
```

> **Note:** You can load any compatible ST20 binary. Ensure you load a file before starting execution; attempting to run the emulator with empty memory will result in immediate execution errors.

The emulator reads configuration values from `st20emu.ini`, which must reside in the same directory as the executable. The settings in this file are straightforward and self-explanatory.

`ZERO_FILL_START`/`ZERO_FILL_END` (inclusive, hexadecimal) set an address range whose unwritten bytes read as zero instead of stopping with an uninitialised-memory error. The shipped file covers SDRAM `0xc0000000`-`0xc07fffff`, where boot loaders such as the DSR9500's read a warm-restart mailbox before anything has written it. Remove both lines to trap on every unwritten byte.

### SoC Selection

Use `--soc sti5518` (the default) or `--soc sti5512` to select the device model.
The selected SoC is printed at startup and included in batch logs.

```bash
./st20emu --soc sti5512 --soc-revision 0xaa --firmware firmware/wsad.bin.bak --batch
```

`VID_REV` at `0x00000078` is a read-only byte. `--soc-revision HEX` selects its value (00
to ff) and is accepted only with STi5512. The default `0x00` is a synthetic emulator value,
not a documented silicon reset value; the example `0xaa` is a firmware comparison value, not
a verified revision for the target board. The register is defined in the STi5512 Register
Manual (7145562 B), pages 189-190, with `VideoBaseAddress = 0` in Table 1.

STi5512 startup also installs ASC0-ASC3 (`0x20003000`-`0x20006000`, interrupts 10, 9, 8
and 7, from the STi5512 datasheet 7145560 B, Table 50), the interrupt controller (INC,
`0x20000000`) and the STi5512 interrupt level controller (INL, `0x20011000`: per-interrupt
priorities and `InputInterrupts`, without the STi5518 trigger-mode and enable extensions).
Unlike the STi5518 model, the INC honours the `GloEn` mask bit (bit 16). The ASC
transmitter model is shared with the STi5518 (`src/soc/common`);
`takeSti5512AscTransmit(port)` returns each port's captured bytes. The register blocks below `0x8000` (video and configuration
`0x0000`, audio `0x0800`, sub-picture `0x1000`, DENC `0x1800`, EMI `0x2000`, cache `0x4000`
and MPEG control `0x5000`) are byte register files with the manual's reset values; writes to
read-only registers are ignored, and decoder, clock and display side effects are not modelled.
The windows follow the datasheet memory map, so reserved locations inside them are storage
too; command bits that the manual says reset automatically read back as 0, and the audio and
DENC soft resets restore reset values.

Further STi5512 devices:

- SSC0/SSC1 (`0x20009000`/`0x2000a000`, interrupts 5 and 6) as I2C masters: START with the
  next TBUF write, 9-bit frames (`(byte << 1) | ack`), STOP, BUSY and soft reset. Slaves are
  register files attached with `attachSti5512I2cDevice`; a missing slave does not acknowledge.
  Slave mode, SPI, arbitration and 10-bit addressing are not modelled.
- PTI DMA channels 1-3 (`0x20031000`): the read pointer advances to the write pointer,
  wrapping from top to base; the decoders are not modelled, so the data is discarded.
- PWM and capture/compare counters (`0x2000b000`, interrupt 11), clocked from the CPU cycle
  count through their prescalers: PWM overflow, compare and capture interrupts. Capture edges
  come from the board (the DSR9500 IR receiver).
- PIO ports 0-4 (`0x2000c000`-`0x20010000`): output, set/clear aliases and configuration
  registers; input pins read back their output unless the board drives them. Compare
  interrupts are not modelled.
- Display timing: 50 Hz PAL fields raise VSYNC top/bottom in `VID_ITS` (interrupt 21). Reading
  `VID_ITS` at `0x3d` samples and clears it.
- OSD output: the `screen FILE` command renders the OSD block lists (`OSD_TOP`/`OSD_BOT`,
  2/4/8 bpp, 14/24-bit palettes, mix weights) over the background colour as a 720x576 BMP.
- MPEG video decoder control: bit buffer (fed by PTI DMA channel 1 or `CFG_CDR`, level in
  `VID_VBL`), start code detector and header FIFO (`VID_HDS`, `VID_HDF`, `SCH`), and decoding
  tasks (`VID_TIS` EXE/FIS/SKP at VSYNC: PSD, then PII/DEI once the picture's data is
  consumed), with soft and pipeline resets. Pictures are not reconstructed, so video and
  still pictures are not displayed; only the OSD is.

The transport controller, smart card, teletext, the MPEG decoders, external RAM reset contents
and device snapshots remain unimplemented. The interactive `vra` register descriptions remain
STi5518-specific.

### Boards

`--board dsr9500` (with `--soc sti5512`) adds the Samsung DSR9500's I2C chips as register
files: the QPSK demodulator (`0xaa` on SSC1, identity `0x5a` in register 0), the CI controller
(`0x80`) and the RF modulator (`0xca`) on SSC0. There is no tuner, signal, CI module or RF
model. The board also connects:

- the front panel: a 16-bit shift register on PIO4 (data bit 3, clock bit 4) latched by PIO1
  bit 7. Its four LED digits are printed as `LED> text` when they change; `led` shows them.
  `fp KEY` holds a key of the matrix (`up`, `down` or a column bit 1, 2, 4, 8, 0x80) for 150 ms.
- the IR receiver on PIO3 bit 3 and PWM CaptureIn0: `ir KEY...` sends NEC frames with the
  customer code 0xfe01. Keys are `0`-`9`, `ok`, `menu`, `exit`, `up`, `down`, `left`, `right`,
  `pgup`, `pgdn`, `power` or a command number. The receiver starts in standby, where only
  `power` (remote or front panel) is accepted.

Stop a free run (`g`) to type these commands, then continue with `g`. With the DSR9500
firmware the main menu appears after about 90 million instructions (`screen menu.bmp`). The DSR9500 main firmware boots with:

```bash
./st20emu-release --soc sti5512 --board dsr9500 --firmware firmware/dsr9500mcmain.bin --batch
```

### Serial Console

Bytes the firmware sends through the ASC ports are printed line by line as `ASCn> text`
(to the terminal, or to the batch log).

### Native STi5518 Devices

STi5518 startup installs independent ASC1 (`0x20004000`, source 9) and ASC2 (`0x20005000`, source 8) transmitter models and an interrupt-controller model. Transmitter-ready events use the firmware-programmed source routing, interrupt masks, vector workspace and native `iret` save area. Sources routed to the same interrupt level are combined. This works with `PORTABLE_INSTRUCTION_ABI = 0x0`; it does not require portable saved-operation formats.

`takeSti5518Asc1Transmit()` and `takeSti5518Asc2Transmit()` retrieve each port's captured outgoing bytes for embedders. Transmission timing is simplified, incoming serial data and full FIFO behavior are not implemented, and device state is not included in snapshots. Firmware may still become idle while waiting for input or another unmodeled peripheral.

### Portable Instruction ABI

Set `PORTABLE_INSTRUCTION_ABI = 0x1` in the configuration to enable the emulator-specific saved-operation and floating-point formats. The default is `0x0`, preserving the reference-oriented fallback behavior. Embedders can use `setPortableInstructionAbi(true)`.

Portable mode adds resumable moves and channel copies, complete tagged shadow records, `restart`, alternative FP formats, and queued/nested external interrupts. It is **not a verified ST20 silicon ABI** and may be incompatible with existing firmware. The record and FP formats, host event APIs, snapshot behavior, and remaining limits are specified in [the instruction status document](docs/instruction-status.md#portable-emulator-abi). Native `postnormsn` and `roundsn` are available without this option and use the separately [simulator-verified intermediate format](docs/instruction-status.md#native-floating-point-helpers).

Explicitly configured FIFO endpoints via `registerExternalChannel`, `feedExternalChannel` and `drainExternalChannel` also work in native mode, including blocking input/output, scalar sends, ALT guards and reset. Native transfers are atomic with RAM-only buffers; portable transfers retain resumable state. These host-configured endpoints are not an automatic STi5518 hardware channel map.

`reboot` performs a boot-from-ROM cold reset: processor and device-model state return to their reset values, memory is kept, and execution restarts at `START_ADDR`. Exact chip-specific bus timings, alignment rules, and peripheral register-map integration are not certified by portable mode.

> **Note:** The INI file is a legacy behaviour of old code. It is expected to be removed and replaced later.

Once your firmware is loaded, you can begin issuing commands to the emulator.

---

### Parameter definitions

Before using the commands, keep in mind these parameter meanings:

- `value` / `address`: A 32-bit **octal** value or address.
- `filename` / `name`: A target file or directory name.
- `register`: The specific register identifier (`a`, `b`, `c`, or `i`).
- `index`: An octal number representing a valid workspace pointer (`Wptr`) index.

---

### Emulator commands reference

| Category               | Command | Syntax               | Description                                                                                                                                                                                                                                                                                                                     |
| ---------------------- | ------- | -------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Execution**          | `ENTER` | _(Press Enter)_      | Executes the single next instruction.                                                                                                                                                                                                                                                                                           |
|                        | `g`     | `g`                  | Runs the emulation from the current address until a watch condition is met or 1,000,000 instructions pass. **Press `g` again during execution to stop.**                                                                                                                                                                        |
|                        | `s`     | `s register value`   | Sets a watch condition on `a`, `b`, `c`, or `i`. When the register hits this value, execution halts and prompts you.                                                                                                                                                                                                            |
| **Register control**   | `a`     | `a value`            | Sets the **A register** to the specified value.                                                                                                                                                                                                                                                                                 |
|                        | `b`     | `b value`            | Sets the **B register** to the specified value.                                                                                                                                                                                                                                                                                 |
|                        | `c`     | `c value`            | Sets the **C register** to the specified value.                                                                                                                                                                                                                                                                                 |
|                        | `i`     | `i value`            | Sets the **Iptr (Instruction pointer)** register to the specified value.                                                                                                                                                                                                                                                        |
|                        | `w`     | `w index value`      | Sets the workspace word at the specified `index` to the given value.                                                                                                                                                                                                                                                            |
| **Memory control**     | `l`     | `l address filename` | Loads a binary/TSOP dump file into memory starting at the specified address.                                                                                                                                                                                                                                                    |
|                        | `v`     | `v address`          | Views the word at the specified memory address.                                                                                                                                                                                                                                                                                 |
|                        | `va`    | `va address value`   | Writes/sets the word at the specified address with a new value.                                                                                                                                                                                                                                                                 |
|                        | `vaa`   | `vaa address range`  | Views a range of words starting from the specified address.                                                                                                                                                                                                                                                                     |
| **State & session**    | `load`  | `load name`          | Loads a previously saved ST20 state.                                                                                                                                                                                                                                                                                            |
|                        | `save`  | `save name`          | Dumps memory contents (in 8K chunks) and the CPU state into a new directory named `name`. The `.bin` files are the memory contents. These can be loaded into a hex editors or IDA Pro. The `.use` files are flags indicating if a particular byte in memory has been defined or not. The `cpu.bin` file contains the CPU state. |
|                        | `q`     | `q`                  | Quits the emulator.                                                                                                                                                                                                                                                                                                             |
| **Diagnostics & info** | `db`    | `db value`           | Searches the internal database for information about a specified register (passed as a hex number).                                                                                                                                                                                                                             |
|                        | `omr`   | `omr`                | Displays "Other Machine Registers" (clock, status registers, traps, interrupts).                                                                                                                                                                                                                                                |
|                        | `ver`   | `ver`                | Shows the contents of the "Enables" register with descriptive labels for every bit set to 1.                                                                                                                                                                                                                                    |
|                        | `vra`   | `vra`                | Toggles STi5518 register and memory-region descriptions for `devlb`, `devls`, `devlw`, `devsb`, `devss`, and `devsw`. Standard device-access notes are printed in either mode. |

Only version-11 CPU snapshots are supported; unversioned files, older versions (including version 10), and incomplete runtime blocks are rejected without changing live state. CPU registers, addresses, watch values, and memory words now use `uint32_t` rather than host-sized integers. Unused status and workspace-use slots are reserved and written as zero; they are no longer maintained in live CPU state. Snapshot records still use native byte order, boolean representation, and structure padding.

The `UNDEFINED_WORD` setting supplies the 32-bit pattern used for undefined registers, memory values, and instruction results. `TIMER_GUESS` supplies the initial value of both priority clocks.

---

### Execution output

After a command is executed or `ENTER` is pressed, the emulator prints standard status block:

- Displays the current hex values for `A`, `B`, `C` and the `Iptr` registers
- Displays all currently allocated workspace words.

      > Note: Undefined words use `UNDEFINED_WORD`, which defaults to `0xCCCCCCCC`.

- Displays the memory address of the upcoming command, its raw octal byte values, and its decoded assembly mnemonic.
- Finally, it drops the cursor back to the command prompt (`>`), waiting for the input.

Here's an example of what you might see:

```plaintext
A=0x7fff0014 B=0xaaaaaaaa C=0xaaaaaaaa  Iptr=0x7fff0038
Wptr  0=0x7fff0014  1=0x7ffffff0  2=0xaaaaaaaa 3=0xaaaaaaaa
      4=0x7ffffff0

7fff0038  60 bd  ajw fffffffd
>
```

If you have `vra` mode turned on, you may see these additional diagnostic tags:

- `NOTE`: Standard informational note generated by `st20emu`.
- `REGN`: **Reg**ister **N**ame found in the internal hardware database.
- `bits:`: The bit-width extension of the register.
- `access:`: Allowed access permissions for the register (e.g., `R/W` for Read/Write).
- `DESC`/`DESCR`: Text **desc**ription of the register or the targeted memory region.

And this with the `vra` mode ON (if an entry is found in the internal DB):

```plaintext
7fff01a4  2f f1  devsb
>
NOTE: At 0x7fff01a4 Write to device at address 00000e00, value=0x00000007
REGN: MPEG_CONTROL_7_0 bits:8 access:R/W
DESC: MPEG Audio/Video buffer control register
A=0x7fff01d2 B=0xaaaaaaaa C=0xaaaaaaaa  Iptr=0x7fff01a6
Wptr  0=0x7fff01d2  1=0xaaaaaaaa  2=0xaaaaaaaa  3=0xaaaaaaaa
      4=0x7ffffff0  5=0xaaaaaaaa  6=0xaaaaaaaa

7fff01a6  27 40  ldc 70
>
```

This is the output of the `omr` command...

```plaintext

> omr
OTHER MACHINE REGISTERS

Enables=0xffffc000
ClockRegHP=0x20000000 ClockRegLP=0x20000000 ClockEnables=0x03
HP_ErrFlag=0x00 LP_ErrFlag=0x00 HaltOnError=0x00
>

```

...and this for `ver`

```plaintext
> ver
Enables Register Value=0xffffc000
 LP_PROCESS_INT_ENB     is set
 LP_TIMESLICE_ENB       is set
 LP_EXTERNALEVENT_ENB   is set
 LP_TIMER_ALRM_ENB      is set
 HP_PROCESS_INT_ENB     is set
 HP_TIMESLICE_ENB       is set
 HP_EXTERNALEVENT_ENB   is set
 HP_TIMER_ALRM_ENB      is set
>
```

---

`devls`/`lb`/`lw` and `devss`/`sb`/`sw` can be used for memory access too, so if a specific address isn't found in the internal DB, a more generic description is returned based on the datasheet memory map description.

Example:

```plaintext
NOTE: At 0xc0402804 Write to device at address c042a5e8, value=0x00000002
DESCR: Address is into Shared SDRAM (Region 1)
```

---

The ajw command has not been executed yet. If you hit the ENTER
key, it will be executed and the results of its execution will be
displayed.

## Acknowledgments

This project is a fork of the original [ST20Emu](https://github.com/IU5HKU/ST20Emu) created by Marco Campinoti.

## License

This project is licensed under the terms of the [GPL-3.0 license](https://github.com/StachowiakDawid/ST20Emu/blob/main/LICENSE).
