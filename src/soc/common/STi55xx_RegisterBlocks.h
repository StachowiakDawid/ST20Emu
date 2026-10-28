#ifndef STI55XX_REGISTER_BLOCKS_H
#define STI55XX_REGISTER_BLOCKS_H

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

/* A block of byte registers of the MPEG decoder (video, audio, DENC and others) as plain
 * storage: writes to read-only registers are ignored, reads return the last written or reset
 * value, and self-clearing command bits read back as 0. */
struct RegisterBlock {
  uint32_t base;
  uint32_t size;
  std::vector<std::pair<uint32_t, uint8_t>> resets;
  std::vector<std::pair<uint32_t, uint32_t>> readOnly;       // inclusive offset ranges
  std::vector<std::pair<uint32_t, uint8_t>> selfClearing{};  // offset, bits reset automatically
  std::vector<uint8_t> values{};
};

/* zeroes the registers from FIRST on and restores their reset values */
void resetRegisterBlock(RegisterBlock &block, uint32_t first = 0);
/* stores a byte, then clears its self-clearing bits */
void storeRegisterByte(RegisterBlock &block, uint32_t offset, uint8_t data);

/* the SoC's handling of one byte: READ returns it, WRITE stores it (storeRegisterByte unless
 * the register has side effects) */
using RegisterByteRead = uint8_t (*)(RegisterBlock &block, uint32_t offset);
using RegisterByteWrite = void (*)(RegisterBlock &block, uint32_t offset, uint8_t data);
/* little-endian access of WIDTH bytes at OFFSET; WRITE skips read-only registers */
int readRegisterBytes(RegisterBlock &block, uint32_t offset, int width, uint32_t *value, RegisterByteRead read);
int writeRegisterBytes(RegisterBlock &block, uint32_t offset, int width, uint32_t value, RegisterByteWrite write);

/*
 * Video registers the STi5512 and STi5518 share at the same offsets: the OSD pointers
 * (OSD_TOP/OSD_BOT, called OSD_OTP/OSD_OBP on the STi5518) at 0x2a/0x2b, serial registers
 * written high byte first; the interrupt mask VID_ITM (bits 23:16 at 0x3c, 15:8 at 0x60, 7:0
 * at 0x61) and status VID_ITS (0x3d, 0x62, 0x63), which latches the events the display model
 * raises.
 */
struct VideoInterruptRegisters {
  /* reading 0x3d samples and clears every event, 0x62/0x63 return the sample (STi5512);
   * otherwise each byte clears when read */
  bool sampleOnStatusRead;
  std::array<uint16_t, 2> osdPointers{};
  std::array<bool, 2> osdLowByte{};
  uint32_t interrupts = 0;
  uint32_t sample = 0;

  void reset();
  /* a VID_ITS byte; false for other registers */
  bool read(uint32_t offset, uint8_t *value);
  /* consumes an OSD pointer byte; false for other registers */
  bool write(uint32_t offset, uint8_t data);
  /* first OSD block of the top (0) or bottom (1) field, in units of 256 bytes of SDRAM */
  uint32_t osdPointer(int field) const;
  /* sets VID_ITS events: bits 23:16 = 0x3d, 15:8 = 0x62, 7:0 = 0x63 */
  void raise(uint32_t events);
  /* the video interrupt line: an event that VID_ITM in VIDEO enables */
  bool line(const RegisterBlock &video) const;
};

#endif
