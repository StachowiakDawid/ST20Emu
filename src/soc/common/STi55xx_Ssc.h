#ifndef STI55XX_SSC_H
#define STI55XX_SSC_H

#include <array>
#include <cstdint>
#include <vector>

/* I2C slave modelled as a register file: the first byte written after the address selects
 * the register, further bytes are written there and reads return it; the pointer then
 * advances. */
struct I2cRegisterDevice {
  uint8_t address; // 8-bit bus address with the R/W bit clear
  std::array<uint8_t, 256> registers{};
  uint8_t pointer = 0;
  bool pointerWritten = false;
};

/* SSC (synchronous serial controller) in I2C master mode. Registers are 16 bits wide at
 * word offsets 0 BRG, 1 TBUF, 2 RBUF, 3 CON, 4 IEN, 5 STAT, 6 I2C, 7 SLAD. Frames are
 * 9 bits: (byte << 1) | acknowledge. */
struct SscDevice {
  uint32_t base;
  uint32_t source;
  std::array<uint32_t, 8> registers{};
  std::vector<I2cRegisterDevice *> slaves{};
  bool busActive = false;
  bool addressFrame = false;
  bool reading = false;
  I2cRegisterDevice *selected = nullptr;
  uint32_t frameDelay = 0;
  uint32_t pendingReceive = 0;
};

inline constexpr uint32_t sscRegisterEnd = 0x1f;

int readSsc(void *context, uint32_t address, int width, uint32_t *value);
int writeSsc(void *context, uint32_t address, int width, uint32_t value);
void resetSsc(SscDevice &device);
/* completes a frame in progress; returns the device's interrupt line */
bool tickSsc(SscDevice &device);

#endif
