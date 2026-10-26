# Flashing the DSR9500 over RS-232

This describes how the Samsung DSR9500's boot loader receives a new main
application over the serial port and writes it to flash, and how it sends the
installed application back out. Everything here comes from the byte-exact
decompiled boot loader.

- `main.c`: boot decision, `receive_image`, `send_image`, `load_image`, `check_image`, `build_header`
- `m_ab80.c`: ASC (UART) driver and XMODEM packet layer
- `hw.c`: flash writer

Where something is inferred rather than read directly from the code, it is
marked as such.

## Overview

1. At power-on, the boot loader decides whether to start the main
   application or to run a serial transfer.
2. In **download** mode it receives an image with **XMODEM-1K**, as a host
   would send it with any terminal program that supports XMODEM-1K.
3. It checks the image's model number and a 16-bit byte sum.
4. It builds the flash header and erases and programs the main-application
   area starting at `0x7fe00000`.
5. It reboots into the new application.

The boot loader itself (`0x7ffe0000`-`0x7fffffff`) is never rewritten by this
path.

The firmware update files distributed for this receiver (for example
`SamMultiCas100/100.bin`) are exactly the byte stream the boot loader
expects: a 16-byte header followed by the application body, padded with
`0x1a`. They can be sent as-is with an XMODEM-1K sender.

## Serial port settings

From `asc_init`:

| Setting | Value |
|---|---|
| UART | STi5512 ASC at `0x20006000`, interrupt level 7, 0x411-byte receive ring |
| Frame | 8 data bits, no parity, 1 stop bit (control register `0x189`) |
| Baud divisor | `0x1b` (27); `0x20` (32) on boards whose config register `0x20000500` reads `1` in its low 6 bits |
| Baud rate | Probably 115,200 (inferred: 27 × 16 × 115,200 ≈ 50 MHz peripheral clock; the clock is not in the code) |
| Flow control | None |

During a transfer the driver is switched to raw mode, with no LF to CR LF
expansion (`asc_set_raw`), and console output is muted (`asc_set_quiet`).

## Entering download mode

After hardware set-up and the banner, `main` checks three sources in this
order. The first one that requests something wins:

1. **Front-panel keys held at power-on** (`read_boot_switches`, via
   `fp_get_keys`):
   - bit 0: **download** (receive an image);
   - bit 1: **upload** (send the installed image).
2. **Console key** (`wait_download_key`): the loader prints `Select Download`
   and polls the serial port 30 times, 600 ticks apart:
   - `d`: download;
   - `u`: upload;
   - `w` or `W`: over-the-air download (not serial; see the last section).

   If nothing arrives, it prints `Timeout`.
3. **A request from the main application** (`f_c05f1ed4`): 1 = download,
   2 = upload, 3 = over-the-air. This is probably the request code the
   application leaves in the warm-restart mailbox at `0xc0400000` before
   rebooting (inferred).

If nothing is requested, the loader validates the installed image
(`check_image`). If it is valid, the loader starts it; if not, it falls back to
over-the-air crash recovery.

The front-panel display shows status codes during the transfer (`show_code`):

| Code | Meaning |
|---|---|
| 12 / 13 | Download started (13 when started by a front-panel key) |
| 14 | Image received correctly |
| 15 | Receive failed |
| 38 | Flash programming |
| 16 / 17 | Upload started |
| 19 | Upload succeeded |
| 18 | Upload failed |

## Transfer protocol: XMODEM-1K with CRC

The boot loader is the **receiver**, and the PC is the **sender**.

| Byte | Meaning |
|---|---|
| `C` (0x43) | Receiver asks for CRC mode and starts the transfer |
| `STX` (0x02) | Start of a 1024-byte block |
| `SOH` (0x01) | Start of a 128-byte block (accepted) |
| `EOT` (0x04) | End of transfer |
| `ACK` (0x06) | Block accepted |
| `NAK` (0x15) | Block rejected, resend |

Block format, as sent by `send_packet` and read by `receive_packet`:

```
STX | seq | 0xff - seq | 1024 data bytes | CRC-16 high | CRC-16 low
```

- The CRC is CRC-16/CCITT, polynomial `0x1021` with initial value 0 (XMODEM-CRC),
  over the 1024 data bytes (`crc16_ccitt`).
- Sequence numbers start at 1 and wrap modulo 256.

Receiver behaviour (`receive_packet`):

- At the start it sends `C` up to 32 times and listens for `STX`, `SOH` or
  `EOT` after each one.
- After each block it answers `ACK`, or `NAK` if the sequence number or its
  complement is wrong.
- `EOT` is answered with `ACK` and ends the transfer.
- A block that fails ten times aborts the transfer (it sends `EOT`).
  `receive_image` retries the header block, and later blocks that time out,
  up to 10 times.
- **The receiver reads the two CRC bytes but never checks them.** The CRC it
  compares against is never computed from the data. Integrity rests on the
  sequence numbers and on the 16-bit byte sum in the image header (below).

Sender behaviour (`send_packet`, used for uploads):

- Before the first block it waits for `C` (`xmodem_wait_sync`; error "Sync byte
  timeout").
- It sends each block and waits for `ACK` or `NAK`, retrying a `NAK`ed block up
  to 10 times.
- It finishes with `EOT`.
- The last block is padded with `0x1a` (SUB), the classic XMODEM fill byte.

## Image format (the payload)

The first 16 bytes of the first block are a header (big-endian). The
application body follows directly, starting at byte 16 of the first block:

| Bytes | Field | Checked on receive |
|---|---|---|
| 0-1 | Model `0x0309` | Yes: anything else gives "Error: Diffrent modell" |
| 2-5 | Body length in bytes | Used as the byte count |
| 6-9 | End address in flash (`0x7fe00024` + length) | No (written by the sender only) |
| 10-11 | 16-bit sum of all body bytes | Yes: "Error: Checksum fail" |
| 12-13 | Software version (for example `0x0064` = 100) | Stored in the flash header |
| 14-15 | `0` | No |

Bytes after the body (the `0x1a` padding) are ignored: the receiver stops
copying when the length is used up.

Example: the header of `SamMultiCas100/100.bin`:

```
03 09 | 00 18 67 00 | 7f f8 67 24 | 43 4a | 00 64 | 00 00
model   len 0x186700  end 0x7ff86724 sum    ver 100
```

The body's byte sum is `0x434a`, matching bytes 10-11. The body is the
romimage that ends up at flash `0x7fe00024`.

`Samsung7000_109_EMU_03.2007_przez_Rs232.bin` in this folder (a DSR7000
image) has the same header format: model `0x0309`, length `0x1dffd0`, and a
byte sum `0xa91b` that matches its body. Its bytes 6-9 hold `0x7fc00000`, not
"base + length". The DSR9500 loader never reads that field, so it is only
information from the sender.

On success the loader prints `Checksum: OK(xxxx)`. On failure it prints one of
these errors:

- "Error: Transmitter not responding"
- "Error: Diffrent modell"
- "Serious error while receiving: %X"
- "Error: Checksum fail"

## Writing the flash

`load_image` then performs three steps:

1. **Build the flash header** (`build_header`), 36 bytes in front of the body:

   | Offset | Content |
   |---|---|
   | `+0x00` | Magic `0x01182000` |
   | `+0x04` | Body length |
   | `+0x08` | Software version (header bytes 12-13) |
   | `+0x0c` | Hardware id (`0x504` on this unit) |
   | `+0x10` | Checksum of bytes `0x00`-`0x0f` |
   | `+0x14` | Checksum of bytes `0x00`-`0x13` |
   | `+0x18` | Checksum of the body |
   | `+0x1c` | Zero |
   | `+0x24` | Body |

   The checksums come from `checksum_header` and `checksum_body`, which use a
   CRC table.

2. **Program the flash** (`flash_write`), header plus body, starting at
   `0x7fe00000`:
   - The code selects a sector table by flash chip ID.
   - It erases each sector the image touches with AMD-style command
     sequences: unlock `0xaaaa` to `…aaaa`, `0x5555` to `…5554`; erase setup
     `0x8080`; sector erase `0x3030` or `0x5050`. Each command byte is
     duplicated in both halves of a 16-bit word, so the flash is probably two
     8-bit chips side by side (inferred).
   - It programs the image as 16-bit words, with the front-panel progress bar
     running.
   - Only the sectors under the new image are erased. That is why a stale tail
     of an older, longer image can survive at `0x7ff90000`.
3. **Reboot.** On the next boot, `check_image` verifies:
   - the magic (display code 7 if wrong);
   - both header checksums (codes 8 and 9);
   - the body checksum (code 10).

   It then starts the application through the reset stub at the end of the
   body.

## Uploading (reading the firmware out)

Upload mode (`send_image(5)`) is the reverse. The loader acts as an
XMODEM-1K **sender** and transmits the installed application in the same
format:

1. It reads the body, length and version from the flash header at
   `0x7fe00000`.
2. It computes the 16-bit byte sum.
3. It builds the same 16-byte header (model, length, end address, sum,
   version).
4. It sends header plus body in 1 KiB blocks, padded with `0x1a`, then `EOT`.

A file received this way is directly re-flashable. The upload errors are:

- "Sync byte timeout"
- "Writing to UART error"
- "ACK Timeout"
- "End of retry limit"
- "Error in EOT X1K_send"
- "Unknown error"

## Not serial: over-the-air download

The `w` key, mailbox request 3, and the crash-recovery path use a different
method. The loader tunes a fixed satellite transponder and PID table, reads two
info tables ("[OTA] SW is present"), and receives the image from the broadcast
(`ota_download`). It then writes it with the same `build_header` and
`flash_write` steps.
