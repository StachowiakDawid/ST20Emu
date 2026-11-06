#ifndef DSR9500_H
#define DSR9500_H

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

/* Samsung STi5512 front panel (16-bit LED/key-matrix shift register on PIO) and NEC remote
 * (IR receiver on a PIO bit and PWM CaptureIn0), shared by the DSR9500 and DSR7000 boards.
 * The shift register is clocked by shiftPort.clockBit with data on dataBit and latched by
 * latchPort.latchBit; bits 12-15 select a digit, bits 0-7 are its active-low segments. A
 * pressed key connects the matrix column it is on to keyReturnPort.keyReturnBit. */
struct SamsungPanelConfig {
  int latchPort;
  uint8_t latchBit;
  int shiftPort;
  uint8_t dataBit;
  uint8_t clockBit;
  int keyReturnPort;
  uint8_t keyReturnBit;
  int irPort;
  uint8_t irBit;
  uint32_t necCustomer;
  std::span<const uint8_t, 96> segments; // ASCII 0x20-0x7f -> segments, for `led`
  std::span<const std::pair<std::string_view, int>> irKeys;
  std::span<const std::pair<std::string_view, int>> panelKeys;
  /* optional further board logic on the same PIO ports (bit-banged I2C) */
  void (*observeExtra)(int port, uint8_t out) = nullptr;
  uint8_t (*inputExtra)(int port, uint8_t levels) = nullptr;
};

/* installs the panel and remote model (PIO observer/input source, board poller) */
int initSamsungPanel(const SamsungPanelConfig &panel);

/* Samsung DSR9500 board parts outside the STi5512; requires initSti5512Devices() first. */
int initDsr9500Board();

/* Sends one NEC remote-control frame. KEY is a name (0-9, ok, menu, exit, up, down, left,
 * right, pgup, pgdn, power) or a command number. Returns an error message, or "" on success. */
std::string dsr9500SendIr(std::string_view key);
/* Holds a front-panel key long enough for the firmware's debounce. KEY is up, down, power or
 * a matrix column bit (1, 2, 4, 8, 0x80). Returns an error message, or "" on success. */
std::string dsr9500PressFrontPanel(std::string_view key);
/* Text currently shown on the 4-digit front-panel display. */
std::string dsr9500Display();
/* The four digits' active-low segment bytes, as hex. */
std::string dsr9500DisplaySegments();

#endif
