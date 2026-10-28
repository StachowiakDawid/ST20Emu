#ifndef STI55XX_ASC_H
#define STI55XX_ASC_H

#include <array>
#include <cstdint>
#include <deque>
#include <vector>

/* ASC (UART) model shared by the STi5512 and STi5518, whose ASC register blocks are
 * identical. Registers are word-sized at base + 0x00 to 0x28. Received bytes wait in
 * `received` and enter RxBuffer one at a time (no FIFO, no overrun) while RxEnable and Run
 * are set. With a transmit observer, sent bytes go to it instead of `transmitted`. */
struct AscDevice {
  uint32_t base;
  uint32_t source;
  std::array<uint32_t, 11> registers{};
  bool previousSource = false;
  uint32_t transmitDelay = 0;
  uint32_t receiveDelay = 0;
  std::vector<uint8_t> transmitted{};
  std::deque<uint8_t> received{};
  void (*transmitObserver)(uint8_t byte) = nullptr;
};

inline constexpr uint32_t ascRegisterEnd = 0x2b;

int checkWordAccess(uint32_t address, int width);
int readAsc(void *context, uint32_t address, int width, uint32_t *value);
int writeAsc(void *context, uint32_t address, int width, uint32_t value);
/* register reset; captured transmit bytes are kept */
void resetAsc(AscDevice &device);
/* queues bytes on the receive line */
void feedAscReceive(AscDevice &device, const std::vector<uint8_t> &bytes);
/* advances the transmit delay; returns the device's interrupt line */
bool tickAsc(AscDevice &device);

#endif
