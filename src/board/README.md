# Boards

## Samsung DSR9500

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
./builddir-release/st20emu --soc sti5512 --board dsr9500 --firmware firmware/dsr9500mcmain.bin --batch
```

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
  code `0xfd01` and command `KEY`, `menu` (`0xdc`) or a hex byte.

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
`ST20_PRINT_HOOK=7fef25d6,7ff00898`).
