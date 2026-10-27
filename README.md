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

Batch mode runs until the first fetch or execution error (including a CPU halt, or an idle CPU
that no peripheral interrupt wakes within 10 emulated seconds), then exits
with status `1`. Firmware-loading failures also exit with `1`, invalid arguments with `2`.
Batch mode reads no keyboard input and ignores watches and instruction-count limits.

Every instruction, with its CPU state and diagnostics, is logged to
`st20emu-log-YYYYMMDD-HHMMSS.txt` in the current directory, so logs grow quickly; use
interactive mode for long runs. Output still buffered when the emulator is killed is lost.

By default the image is placed immediately below `0x80000000` and execution starts at
`START_ADDR` (default `0x7ffffffe`). To load and start at another address:

```bash
./st20emu --firmware firmware/program.bin --address 0x1000 --batch
```

Omit `--batch` to run interactively. Use `--help` for all options.

Undefined registers, memory values and instruction results read as `0xCCCCCCCC`. Reading
unwritten memory stops with an error, except in the shared SDRAM (`0xc0000000`-`0xc07fffff`),
which reads as zero because boot loaders such as the DSR9500's read a warm-restart mailbox
there before anything has written it.

### SoC Selection

Use `--soc sti5518` (the default) or `--soc sti5512` to select the device model;
`--soc-revision HEX` sets the STi5512 `VID_REV` byte. The modelled devices and their limits are
described in [src/soc/sti5512/README.md](src/soc/sti5512/README.md) and
[src/soc/sti5518/README.md](src/soc/sti5518/README.md).

### Boards

`--board dsr9500` (with `--soc sti5512`) adds the Samsung DSR9500's board devices and the
`fp`, `ir` and `led` commands. See [src/board/README.md](src/board/README.md).

### Serial Console

Bytes the firmware sends through the ASC ports are printed line by line as `ASCn> text`
(to the terminal, or to the batch log).

### Emulator commands

Values, addresses and indexes are hexadecimal; `register` is `a`, `b`, `c` or `i`.

| Command | Syntax | Description |
| --- | --- | --- |
| `ENTER` | _(Press Enter)_ | Executes the next instruction. |
| `g` | `g` | Runs until a watch condition is met or 10,000,000 instructions pass. Type `g` + `ENTER` again to stop; other commands typed meanwhile run at the next prompt. |
| `s` | `s register value` | Stops execution when the register reaches the value. |
| `a`, `b`, `c`, `i` | `a value` | Sets the A, B, C register or Iptr. |
| `w` | `w index value` | Sets workspace word `index`. |
| `vw` | `vw index` | Shows the address of workspace word `index`. |
| `v` | `v address` | Shows the word at the address. |
| `va` | `va address value` | Writes the word at the address. |
| `vaa` | `vaa address length` | Shows `length` bytes of words from the address. |
| `save` | `save name` | Saves memory (8 KB `.bin` blocks with `.use` maps of written bytes) and the CPU state (`cpu.bin`) into directory `name`. |
| `load` | `load name` | Loads a state saved with `save`. |
| `omr` | `omr` | Shows the other machine registers: enables, clocks, timer and ready queues, error flags. |
| `ver` | `ver` | Lists the set bits of the Enables register. |
| `screen` | `screen [file]` | Writes the STi5512 OSD as a BMP (default `screen.bmp`). |
| `q` | `q` | Quits. |

After each step the emulator prints the registers, the workspace and the next instruction:

```plaintext
A=0xcccccccc B=0xcccccccc C=0xcccccccc  Iptr=0x7ffffffe
Wptr=0x1fffffe4 Priority=1 State=running Status=0x00100000
Wptr
 00=0xcccccccc 01=0xcccccccc 02=0xcccccccc 03=0xcccccccc
 04=0xcccccccc 05=0xcccccccc 06=0xcccccccc

7ffffffe  61 07  j loc_7fffffe7
>
```

## Acknowledgments

This project is a fork of the original [ST20Emu](https://github.com/IU5HKU/ST20Emu) created by Marco Campinoti.

## License

This project is licensed under the terms of the [GPL-3.0 license](https://github.com/StachowiakDawid/ST20Emu/blob/main/LICENSE).
