# STi5512

Selected with `--soc sti5512`.

The configuration word at `0x20000500` (board clock mode) reads 0 unless the board sets it
(`setSti5512ConfigWord`; the DSR9500 sets 1). The DSR9500 loader picks its ASC baud reload from
its low six bits (1: 0x20, else 0x1b, i.e. a 60 or 50 MHz board), and the main application's
STAPI `GetClockInfo` maps bits 1:0 to 1 = 60 MHz CPU with 1 MHz/15625 Hz timers, 2 = 39.9 MHz,
3 = 49.875 MHz, and fails for 0: the drivers then read clock rates of 0, which (among others)
turns STAUD's A/V synchronisation pause into no pause, so audio never unmutes.

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
`sti5512Peripherals().takeAscTransmit(port)` returns each port's captured bytes. The register blocks below `0x8000` (video and configuration
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
- PTI DMA channels 1-3 (`0x20031000`): the read pointer advances to the write pointer and wraps
  to base after `top | 0xf` (the drivers write top = base + size - 16); channel 1 feeds the
  video decoder, channel 2 the audio decoder (each stalls while its input is full), channel 3
  discards. `sti5512PtiDmaRegister`/`sti5512PtiDmaSetWrite` let the TC model read the read
  pointers and advance the write pointers.
- PTI (`STi5512_Pti.cpp`, interrupt 14): IntStatus/IntEnable/IntAck (`0x20030000`-`0x2c`), TCMode
  (`0x30`), AudPTS/VidPTS (read the STC) and STCTimer (`0x50`/`0x54`), plus a high-level transport
  controller that interprets the tables ST's PTI driver 3.4.1/3.6.0 keeps in the TC data memory
  (`0x20038000`) instead of running the microcode loaded at `0x2003c000`: the PID table (48
  half-words), slot descriptors (`+0x60`, 16 bytes: section-state and DMA-descriptor pointers,
  configuration word), DMA descriptors (`+0x360`, base/top/read/write), section-state blocks
  (`+0x680`, word 0 = filter mask), the 32 hardware section filters (`0x20034000`; filter byte 0
  = table_id, bytes 1-7 = section bytes 3-9), the filter enable mask (`+0xa6c`), the PCR PID
  (`+0xa40`) and PCR/STC captures (`+0xa44`-`+0xa4c`, interrupt bit 8 of IntStatus0) and the
  running STC (`+0xa64`/`+0xa68`). Matching packets become complete sections, PES packets (with
  the drivers' 4-byte start-address trailer), partial-PES records or raw packets in the slot's
  ring with the slot's interrupt bit, or A/V payload in the ring of DMA channel 1-3 (whose write
  pointer it advances). Input runs while TCMode is 1 and IIFFIFOEnable (`0x20032008`) bit 0 is
  set; packets come from the board (`sti5512PtiInput`, the DSR9500 front end). The full table
  layout is in the comment at the top of `STi5512_Pti.cpp`. Descramblers, alternate output and
  the carousel are not modelled. `ST20_PTI_TRACE=1` prints each slot's set-up when its first
  packet arrives.
- PWM and capture/compare counters (`0x2000b000`, interrupt 11), clocked from the CPU cycle
  count through their prescalers: PWM overflow, compare and capture interrupts. Capture edges
  come from the board (the DSR9500 and DSR7000 IR receivers). A board can set the system
  clock that drives the capture counter (`PwmDevice::systemClockHz`, the DSR7000's 50 MHz).
- Serial video registers OSD_TOP, OSD_BOT (`0x2a`, `0x2b`) and VID_DFS (`0x24`): reads and
  writes share one byte toggle, high byte first (Register Manual: "Ser. R/W"). The DSR7000
  firmware writes and reads them back in a self-test; a mismatch there leaves the OSD
  pointers misaligned.
- PIO ports 0-4 (`0x2000c000`-`0x20010000`): output, set/clear aliases and configuration
  registers; input pins read back their output unless the board drives them. Compare
  interrupts are not modelled.
- Display timing: 50 Hz PAL fields raise VSYNC top/bottom in `VID_ITS` (interrupt 21). Reading
  `VID_ITS` at `0x3d` samples and clears it (de-asserting the interrupt); each of `0x62`/`0x63`
  then returns its sampled byte plus any event raised since and clears when read, so the
  video handlers' re-read of `0x62` after a sequence end code sees only new events instead of
  the stale start code hit (which left the DSR7000 handler looping on an empty header FIFO).
- OSD output: the `screen FILE` command renders the OSD block lists (`OSD_TOP`/`OSD_BOT`,
  2/4/8 bpp, 14/24-bit palettes, mix weights) over the video plane as a 720x576 BMP. The video
  plane is the decoded picture the display shows (black, Y=16, when `VID_DCF.EVD` is clear or
  nothing is decoded), mixed with the background colour by `VID_MWV` (0xff background, 0 video).
- MPEG video decoder: PES parser (automatic mode: PES headers are stripped, elementary streams
  pass; the PTS of the PES a picture starts in appears in `PES_TS` 0x49-0x4d with TSA), bit
  buffer (fed by PTI DMA channel 1 or `CFG_CDR`, level in `VID_VBL`), start code detector and
  header FIFO (`VID_HDS`, `VID_HDF`, `SCH`; the FIFO keeps its bytes when a task consumes the
  bit buffer, so the header of a start code found during a task is still read after it - the
  sequence header found by the first task after STVID_Start, which STVID needs before it
  leaves its start state), and decoding tasks (`VID_TIS` EXE/FIS/SKP at VSYNC:
  PSD, then PII/DEI once the picture's data is consumed; SKP = 11 is a skip-only task), with
  soft and pipeline resets (which abandon a running task). With libavcodec (see below) every
  consumed picture is decoded (in decoding order, as the chip fills frame buffers); the one a
  task decodes is filed under `VID_RFP`, and at each VSYNC
  (unless `VID_DCF.FRZ`) the display takes the picture filed under `VID_DFP`, or the latest one.
  Frame buffers in SDRAM are not written. `video` prints the decoder state,
  `ST20_VIDEO_TRACE=1` its tasks.
- MPEG audio decoder (`0x0800`, `STi5512_AudioDecoder.cpp`): input from PTI DMA channel 2 or
  `AUD_CDI` through the PES parser into the bit buffer (`VID_ABG`-`VID_ABS`, level `VID_ABL`);
  while `AUD_PLY` is set, MPEG-1/2 layer II frames are decoded at their real-time rate
  (`AUD_SYS` = 3, `AUD_HDR`, `AUD_SFR`, `AUD_PTS` = the PTS of the PES packet the frame starts
  in); `AUD_SKP` drops the next frame, `AUD_RES` (any write) and `AUD_RST` flush. `AUD_ITR`
  (interrupt 22, masked by `AUD_ITM`) reports sync status changes, headers, PTS, sampling
  frequency changes and new frames, cleared as the register manual describes (reading
  `AUD_SYS`, `AUD_HDR[31:24]`, `AUD_PTS[32]`, the high byte). `audio FILE.wav [nomute]` captures
  the PCM (silent while `AUD_MUT`, unless `nomute`), `audio stop` finishes the file, `audio`
  prints the state, `ST20_AUDIO_TRACE=1` traces PTS frames and play/stop. CRC, underflow and
  FIFO-threshold interrupts and the PCM output interface are not modelled.

Picture and sound reconstruction use FFmpeg's libavcodec/libavutil when meson finds them
(`-Dlibav=auto`, the default; `enabled` requires them, `disabled` builds without). Without them the
decoders keep their control behaviour and produce no pixels or samples.

With the DSR9500's original firmware, still pictures (the boot logo) decode and display, and a
live channel plays: STVID decodes about 25 pictures a second under A/V sync, the picture is on
screen, and STAUD synchronises and unmutes the audio. The DSR7000 firmware's background still
appears behind its menus as well.

OS20 on these boards installs every interrupt level with `interrupt_flags_low_priority`
(workspace descriptor bit 0), so on the chip the handlers run as low-priority processes. The
emulator still runs them at high priority but makes `ldtimer` return the low-priority clock
inside them: drivers such as STAUD compute `time_now()` + ticks in a handler and wait for it in
a low-priority task.

Smart card, teletext, sub-picture, external RAM reset contents and device snapshots remain
unimplemented.
