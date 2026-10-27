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
./st20emu-release --soc sti5512 --board dsr9500 --firmware firmware/dsr9500mcmain.bin --batch
```
