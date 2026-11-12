# ST20Emu

ST20Emu is an emulator for the ST20, a 32-bit transputer-based processor architecture developed by SGS-Thomson (later STMicroelectronics). Widely used throughout the late 1990s and early 2000s, ST20 cores powered many iconic digital Set-Top Boxes (STBs) and DVD players of the era.

## Development

### Requirements

- clang 18.1.3
- meson and ninja
- just (optional, wraps the commands below)
- FFmpeg 6 or later (optional: `libavcodec`/`libavutil` give the STi5512 video and audio
  decoder models pictures and sound; meson finds them by pkg-config, `-Dlibav=disabled` builds
  without, `-Dlibav=enabled` requires them; the `ffmpeg` tool is used by `tools/mkts.sh`)

### Building

Build a debug version with AddressSanitizer and UBSan as `builddir/st20emu`:

```bash
meson setup builddir
meson compile -C builddir
```

Build an optimized release version (`-O3`, link-time optimization, no sanitizers) as
`builddir-release/st20emu`, in its own directory so it doesn't overwrite the debug build:

```bash
meson setup builddir-release --buildtype=release -Db_sanitize=none -Db_lto=true
meson compile -C builddir-release
```

Use the release build for long firmware runs; the sanitized debug build is many times slower.

Run the tests (CPU instructions; DSR9500 front end, PTI and decoder input):

```bash
meson test -C builddir --print-errorlogs
```

`tools/dvb-firmware-test.sh [EMULATOR]` is a longer end-to-end check with the original DSR9500
firmware (scan, lock, PAT/PMT/SDT, A/V slots, audio capture); under `meson test` it is skipped
unless `ST20EMU_FIRMWARE_TESTS=1`.

With `just` installed, `just build`, `just release` and `just test` run the same steps.
`just pgo` builds a profile-guided release version as `builddir-pgo/st20emu`: an instrumented
build boots the DSR9500 firmware, and the recorded profile steers inlining and branch layout.
`just test-st20sim` runs the differential tests against the ST20 toolset simulator (see
[docs/instruction-status.md](docs/instruction-status.md)).

## Getting started

Run the binary from your terminal:

```bash
./builddir/st20emu
```

For unattended firmware execution:

```bash
./builddir/st20emu --firmware firmware/6300.bin --batch
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
./builddir/st20emu --firmware firmware/program.bin --address 0x1000 --batch
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

### Satellite reception (DSR9500)

`--transponder FILE.ts:FREQ_MHZ:SR_KSPS:H|V[:LO|HI][:FEC]` (repeatable, `--board dsr9500`)
puts a transport stream on air: the modelled HM1221 demodulator locks when the firmware tunes
its frequency, symbol rate, code rate, polarisation and band, and feeds the packets to the PTI
at the channel rate. `tools/mkts.sh OUT.ts [SECONDS] [SR] [FEC]` makes a test stream with two
TV services. `tuner`, `video` and `audio FILE.wav` / `audio stop` show and capture what is
received; see [src/board/README.md](src/board/README.md) and
[src/soc/sti5512/README.md](src/soc/sti5512/README.md).

### Typing into a UART from the CLI or a script

`asc PORT TEXT...` queues TEXT (words joined by single spaces) and a newline on the receiver
of ASC PORT (0-3), e.g. `asc 3 ls -l` for the uClinux shell on the DSR9500's ASC3. It works at
the interactive prompt and in `--script` files.

### Serial Console

Bytes the firmware sends through the ASC ports are printed line by line as `ASCn> text`
(to the terminal, or to the batch log).

`--console PORT` (0-3) turns the terminal into that port's serial terminal: the firmware runs
freely, keys typed go straight into the port's receive queue, and its output is written to the
terminal unchanged (other ports keep the `ASCn>` lines). Press `Ctrl-]` to quit; the run summary
is printed and the emulator exits with `0`. For example, the uClinux shell on the DSR9500's ASC3:

```bash
./builddir-release/st20emu --soc sti5512 --board dsr9500 --firmware uClinux/2.0/dsr9500-uclinux.bin --console 3
```

The terminal is put into raw input mode (restored on exit, also when killed by a signal), so
every key is sent as typed: Enter as CR (a Linux tty's `ICRNL` turns it into a newline),
Backspace as DEL, `Ctrl-C` as `0x03` for the target. Typed input is read every 100,000
instructions. Input may also be piped (`(sleep 7; echo 'echo hi') | st20emu ... --console 3`);
after its end the firmware keeps running. Add `--run COUNT` to stop after COUNT instructions.

`--serial-tcp PORT:TCP` serves ASC PORT's serial line on `127.0.0.1:TCP` (one client at a time;
`0` picks a free port, printed at startup). While a client is connected the port's output goes to
it unchanged instead of the `ASCn>` lines, and what it sends goes into the port's receive queue
(polled every 16,000 instructions); without a client the port prints as usual. It combines with
`--script`, `--run` and the prompt, and with `--console` on another port. For example SLIP between
uClinux 2.4 and a host program (`uClinux/2.4/work/net/slip_host.py`):

```bash
./builddir-release/st20emu --soc sti5512 --board dsr9500 --firmware dsr9500-uclinux24-net.bin \
    --serial-tcp 3:5555 --script slip.script &
python3 slip_host.py --port 5555
```

### Emulator commands

Values, addresses and indexes are hexadecimal; `register` is `a`, `b`, `c` or `i`.

| Command | Syntax | Description |
| --- | --- | --- |
| `ENTER` | _(Press Enter)_ | Executes the next instruction. |
| `g` | `g` + `ENTER` | Runs until a watch condition is met or 10,000,000 instructions pass. Type `g` + `ENTER` again to stop; other commands typed meanwhile run at the next prompt. |
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
| `screen` | `screen [file]` | Writes the screen (video plane and OSD) as a BMP (default `screen.bmp`). |
| `audio` | `audio [file.wav [nomute] \| stop]` | Starts or stops capturing the STi5512 audio decoder's output; without arguments shows its state. |
| `video` | `video` | Shows the STi5512 video decoder's state. |
| `tuner` | `tuner` | Shows the DSR9500 front end: LNB, PLL, symbol rate, lock, packets. |
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
