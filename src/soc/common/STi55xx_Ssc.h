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
  /* serial EEPROM mode (24Cxx): when `memory` is not empty, the first two bytes written after
   * the address are a big-endian offset into it (wrapping at its size) and `registers` and
   * `pointer` are unused */
  std::vector<uint8_t> memory{};
  uint32_t offset = 0;
  int offsetBytesSeen = 0;
  /* optional chip behaviour behind the register file (register mode only): writeHook runs
   * after a byte is stored in register REG; readHook returns the byte a read of REG delivers
   * (STORED is the register file's value). resetI2cDevice keeps them. */
  void *context = nullptr;
  void (*writeHook)(void *context, uint8_t reg, uint8_t value) = nullptr;
  uint8_t (*readHook)(void *context, uint8_t reg, uint8_t stored) = nullptr;
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

/* register file, pointer and EEPROM offset back to power-up (the EEPROM contents stay) */
void resetI2cDevice(I2cRegisterDevice &slave);
/* byte-level slave access, also used for buses other than the SSC (bit-banged I2C on PIO pins):
 * i2cSlaveBeginWrite after an address frame with the R/W bit clear, then i2cSlaveWrite for
 * each byte written and i2cSlaveRead for each byte read */
void i2cSlaveBeginWrite(I2cRegisterDevice &slave);
void i2cSlaveWrite(I2cRegisterDevice &slave, uint8_t byte);
uint8_t i2cSlaveRead(I2cRegisterDevice &slave);

int readSsc(void *context, uint32_t address, int width, uint32_t *value);
int writeSsc(void *context, uint32_t address, int width, uint32_t value);
void resetSsc(SscDevice &device);
/* START (true) or STOP (false) generated on the bus pins by software (outside I2C mode, with
 * the SSC shifting the 9-bit frames in between) */
void sscBusCondition(SscDevice &device, bool start);
/* completes a frame in progress; returns the device's interrupt line */
bool tickSsc(SscDevice &device);

#endif
