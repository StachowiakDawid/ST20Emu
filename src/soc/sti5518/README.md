# STi5518

Selected with `--soc sti5518` (the default).

STi5518 startup installs independent ASC1 (`0x20004000`, source 9) and ASC2 (`0x20005000`, source 8) transmitter models and an interrupt-controller model. Transmitter-ready events use the firmware-programmed source routing, interrupt masks, vector workspace and native `iret` save area. Sources routed to the same interrupt level are combined. The INC is the common model in `src/soc/common`: delivery requires the `INC_MASK` `GloEn` bit (bit 16), and bit 16 of a clear register clears all of its bits.

`takeSti5518Asc1Transmit()` and `takeSti5518Asc2Transmit()` retrieve each port's captured outgoing bytes for embedders. Transmission timing is simplified, incoming serial data and full FIFO behavior are not implemented, and device state is not included in snapshots. Firmware may still become idle while waiting for input or another unmodeled peripheral.
