# STi5512

Selected with `--soc sti5512`.

`VID_REV` at `0x00000078` is a read-only byte. `--soc-revision HEX` selects its value (00
to ff) and is accepted only with STi5512. The default `0x00` is a synthetic emulator value,
not a documented silicon reset value; the example `0xaa` is a firmware comparison value, not
a verified revision for the target board. The register is defined in the STi5512 Register
Manual (7145562 B), pages 189-190, with `VideoBaseAddress = 0` in Table 1.

STi5512 startup also installs ASC0-ASC3 (`0x20003000`-`0x20006000`, interrupts 10, 9, 8
and 7, from the STi5512 datasheet 7145560 B, Table 50), the interrupt controller (INC,
`0x20000000`) and the STi5512 interrupt level controller (INL, `0x20011000`: per-interrupt
priorities and `InputInterrupts`, without the STi5518 trigger-mode and enable extensions).
The INC, ASC transmitter, SSC, PWM, PIO and display/OSD models live in `src/soc/common`, as the
STi5518 has the same blocks (it currently installs only the INC and ASC); the INC
requires the `GloEn` mask bit (bit 16) for delivery, and bit 16 of its clear registers clears
only `GloEn`.
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
and device snapshots remain unimplemented.
