# Boards

## Samsung DSR9500

`--board dsr9500` (with `--soc sti5512`) adds the Samsung DSR9500's board devices:

- the satellite front end (`Dsr9500Frontend.cpp`): the HM1221 QPSK demodulator at I2C `0xaa`
  on SSC1 with the tuner PLL behind its 3-wire port, the LNB supply, 13/18 V and 22 kHz tone on
  the board latch at `0x50400000`, a universal LNB (LO 9750/10600 MHz), and the transponders
  given with `--transponder FILE.ts:FREQ_MHZ:SR_KSPS:H|V[:LO|HI][:FEC]` (repeatable; band
  defaults to HI from 11700 MHz, FEC 1/2-7/8 defaults to 3/4). The demodulator locks when the
  PLL frequency (within 1.5 MHz of FREQ - LO), the symbol rate (within 1 %), the enabled code
  rates, the LNB voltage, tone and supply match a transponder, and reports carrier offset,
  signal level, quality and an error count. While locked it hands the transponder's packets
  (the file in a loop) to the STi5512 PTI at the channel's useful rate SR * 2 * FEC * 188/204,
  paced by emulated time. The register behaviour, as the firmware programs it, is documented
  at the top of `Dsr9500Frontend.cpp`. `tuner` prints LNB, PLL, symbol rate, lock and packet
  counts; `ST20_FRONTEND_TRACE=1` logs latch and demodulator writes. DiSEqC messages are not
  decoded.
- the CI controller (`0x80`) and the RF modulator (`0xca`) on SSC0 as register files (no CI
  module or RF model).
- the 2 MB flash at `0x7fe00000` as an Am29LV160DT (`0x0001`/`0x22c4`, `AmdFlash`): the main
  application saves its settings and channel database there (sector erase with DQ7/DQ3 status,
  unlock-bypass programming from RAM). Without it, saving wrote the AMD command bytes into the
  ROM image and corrupted the code. (The receiver comes up in the main menu with or without the
  flash model; `power` puts it into standby.)
- the configuration word at `0x20000500` reads 1 (clock mode: 60 MHz CPU), which the STAPI
  drivers need for their clock rates.
- the front panel: a 16-bit shift register on PIO4 (data bit 3, clock bit 4) latched by PIO1
  bit 7. Its four LED digits are printed as `LED> text` when they change; `led` shows them.
  `fp KEY` holds a key of the matrix (`up`, `down` or a column bit 1, 2, 4, 8, 0x80) for 150 ms.
- the IR receiver on PIO3 bit 3 and PWM CaptureIn0: `ir KEY...` sends NEC frames with the
  customer code 0xfe01. Keys are `0`-`9`, `ok`, `menu`, `exit`, `up`, `down`, `left`, `right`,
  `pgup`, `pgdn`, `power` or a command number.

Stop a free run (`g`) to type these commands, then continue with `g`. The main menu appears
after about 150 million instructions (`screen menu.bmp`):

```bash
./builddir-release/st20emu --soc sti5512 --board dsr9500 --firmware firmware/dsr9500mcmain.bin --batch
```

### Receiving a test transponder

`tools/mkts.sh OUT.ts [SECONDS] [SR] [FEC]` makes a DVB-like transport stream with two TV
services ("Emu Test 1" and "Emu Test 2": MPEG-2 720x576 25 fps and MPEG-1 layer II 48 kHz
stereo each, PAT/PMT/SDT/NIT) padded to the channel rate. `tools/dvb-firmware-test.sh` puts one on
air at 10788 MHz V 22000 kSym/s 5/6 (the first preset of the firmware's manual scan) and drives
the original firmware: PIN `0000` opens Installation, `5` Manual Scanning, `ok` scans (it finds
2 TV programs) and `ok` saves; after `exit` x3 and a standby/wake cycle (`power` twice) the
channel plays: the firmware's channel task sees the lock, reads PAT, SDT and PMT and starts the
video, audio and PCR slots, the live picture is on screen (`screen FILE`) and the firmware
unmutes the audio once it is in sync (`audio FILE.wav` captures it). (Without the
standby cycle the firmware waits for a lock event the scan already consumed.)

```bash
tools/mkts.sh /tmp/tp.ts 3 22000 5/6
./builddir-release/st20emu --soc sti5512 --board dsr9500 --firmware firmware/dsr9500mcmain.bin \
    --transponder /tmp/tp.ts:10788:22000:V:5/6 --script scan.txt
```

## Samsung DSR7000 (`firmware/dump Samsung DSR7000 (v1.14).bin`)

`--board dsr7000` (with `--soc sti5512`) models the Samsung DSR7000 (Polsat, Nagra, OpenTV EN2),
whose v1.14 flash dump is decompiled byte for byte in `../Decompilation/DSR7000` (the
`a_<addr>.c` sources named below):

- SDRAM at `0x40000000` (static link `0x40001000`); unwritten bytes read as zero, as the
  loader reads its warm-restart mailbox at `0x40000000` before anything writes it.
- the 4 MB flash at `0x7fc00000` as an AMD Am29LV320DT (`0x0001`/`0x22f6`); the loader accepts
  manufacturer 1, 4, `0xec` or `0xbf` and otherwise prints "No supported flash memory".
- two bit-banged I2C buses (SDA bit 0, SCL bit 2; no SSC): PIO1 with the 16 KB serial EEPROM
  (24C128 at `0xa2`, 2-byte offsets; starts erased, not saved) and the RF modulator (`0xca`),
  PIO3 with the LSI 64724 link IC (`0x1c`) and the HM1221 demodulator (`0xaa`) as register
  files. Without the EEPROM the control task aborts and nothing is drawn. The tuner chips
  are not modelled: the firmware reports "Channel Chip not found" and no lock.
- the front panel: the DSR9500's 16-bit shift register with data on PIO4 bit 0, clock PIO4
  bit 1, latch PIO3 bit 5 and the key line on PIO3 bit 4 (`a_7fc33088.c`). `fp KEY`: `power`,
  `chup`, `chdown`, `volup`, `voldown` or a column bit.
- the IR receiver on PIO3 bit 3 and PWM CaptureIn0, NEC customer code `0x0e86`. `ir KEY`:
  `0`-`9`, `menu`, `ok`, `exit`, `up`, `down`, `left`, `right`, `chup`, `chdown`, `volup`,
  `voldown`, `mute`, `power`, `guide`, `guide2`, `info` or a command number. The decoder's
  timing windows fit a 50 MHz system clock, so the capture counter is clocked at 50 MHz
  (`PwmDevice::systemClockHz`) while the CPU stays at 60.75 MHz.

The firmware's debug print, `print_at` (`0x7fc18c29`), is patched in the ROM to return at
once; `ST20_PRINT_HOOK=7fc18c29@c` shows its format strings (`@c`: the format is the third
argument, after the static link and the level). The receiver comes up on the "no signal"
message ("PROBLEM Z ODBIOREM SYGNAŁU...") after about 1 billion instructions; `ir menu` then
opens "Menu Główne":

```bash
printf 'run 1200000000\nir menu\nrun 300000000\nscreen menu.bmp\n' > menu.txt
./builddir-release/st20emu --soc sti5512 --board dsr7000 \
  --firmware "firmware/dump Samsung DSR7000 (v1.14).bin" --script menu.txt
```

`--script FILE` runs `run N` lines (free runs, as `--run`) and CLI commands (`ir`, `fp`, `led`,
`screen`, `dump`, ...) in order, without the interactive prompt.

## 6300 (`firmware/6300.bin`)

`--board 6300` (with the default `--soc sti5518`) models the parts of the receiver whose flash is
`firmware/6300.bin` (an OS20 application with an "EvilEyEs" PC link, 2006):

- the 2 MB flash at `0x7fe00000` as an ST M29W160ET (AMD command set, ID `0x20`/`0x22c4`):
  autoselect, program, sector and chip erase. The boot loader checks the flash ID and the CRC32
  of every 4 KB block before starting the application; settings are written back to flash.
- the front-panel controller on ASC1: 4-byte packets (command, two data bytes, checksum). It
  answers the `0x28` handshake and the application's `0x24` authentication challenge (without
  the right `0x36` answer the application reboots through the watchdog), shows the display
  packets (`led`: active-low 7-segment digits) and sends key packets: `fp power` or `fp HEX`.
- the IR receiver on the IR blaster's RC input: `ir KEY` sends a NEC frame with the customer
  code `0xfd01` and command `KEY`: a key name (`menu`, `ok`, `exit`, arrows, digits, ...) or a
  hex byte.
- PIO4 bit 5 (TV source input) reads high; low makes the application ignore most keys.
- the 8 MB SDRAM mirrored at `0xc0800000`.

Standby is a watchdog reboot; the application only stays on when the controller reports the
power key right after the handshake. `fp power` does that at the next boot, and
`STB6300_POWER=1` holds the power key from power-up:

```bash
STB6300_POWER=1 ./builddir-release/st20emu --board 6300 --firmware firmware/6300.bin
```

Initialisation ends after about 400 million instructions (12 s emulated); `ir menu` then opens
the "Main Page" menu (`screen menu.bmp`). Without a tuner or transport stream nothing plays.

- the QPSK demodulator at I2C address `0xd0` as a register file that acknowledges its address.
  The firmware's I2C driver generates START and STOP on PIO1 (bit 0 SDA, bit 1 SCL) and shifts
  the frames with SSC0; the board reports those bus conditions to the SSC model.

There is no tuner, signal or lock model and no MPEG decoding.

Two environment variables help to follow firmware without sources: `ST20_IOTRACE=N` logs the
first N accesses to each peripheral register (and flash writes) with the instruction address,
plus per-address counts at exit; `ST20_PRINT_HOOK=ADDR,...` prints the format string of every
call to the given firmware printf entry points with the emulated time (for `6300.bin`:
`ST20_PRINT_HOOK=7fef25d6,7ff00898`; `ADDR@c` takes the format from the third argument).
