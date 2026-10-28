# STi5518

Selected with `--soc sti5518` (the default).

STi5518 startup installs these device models (register addresses and interrupt numbers from the STi5518 datasheet 7170179 D and Register Manual 7170180A):

- ASC0-3 (`0x20003000`-`0x20006000`, sources 10, 9, 8, 7): the common UART model, with a receive path fed by board models (`feedSti5518AscReceive`).
- SSC0/1 (`0x20009000`, `0x2000a000`, sources 5, 6): the common SSC model; outside I2C mode a master TBUF write shifts the frame back into RBUF, so software that drives START/STOP on PIO pins sees an unanswered bus.
- PIO0-4 (`0x2000c000`-`0x20010000`) and PIO5 (`0x2000a100`), PWM (`0x2000b000`, source 11).
- the IR blaster's RC-IR receiver (`0x2000a200`, source 18): a four-entry FIFO of pulse/symbol times fed by board models (`STi5518_Irb`).
- the interrupt controller (common INC: delivery requires `INC_MASK` `GloEn`, bit 16 of a clear register clears all of its bits), the interrupt level controller (`0x20011000`, 31 sources) and the low power module's watchdog (`0x20000400`): a watchdog alarm reboots the chip and sets `LPM_WDFLAG`.
- the MPEG register blocks at `0x000`-`0xfff` (video and clock generator, audio, sub-picture, DENC, compressed-data FIFOs, `MPEG_CONTROL`), mirrored at `0x1000`: byte register files with reset values. VSYNC top/bottom are raised every PAL field (video source 28) and `screen` renders the OSD through the common display code. There is no MPEG video or audio decoding.

`sti5518Peripherals().takeAscTransmit(port)` retrieves a port's captured outgoing bytes for embedders. Transmission timing is simplified, there are no ASC time-outs or UART FIFOs, and device state is not included in snapshots.
