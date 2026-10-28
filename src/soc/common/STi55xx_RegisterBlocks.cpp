#include "STi55xx_RegisterBlocks.h"

#include <algorithm>

namespace {
constexpr uint32_t osdTopOffset = 0x2a;           // OSD_TOP, OSD_BOT at 0x2b
constexpr uint32_t interruptMaskOffset = 0x3c;    // VID_ITM bits 23:16; 0x60, 0x61 below
constexpr uint32_t interruptStatusOffset = 0x3d;  // VID_ITS bits 23:16; 0x62, 0x63 below

bool isReadOnly(const RegisterBlock &block, uint32_t offset) {
  for (const auto &[first, last] : block.readOnly) {
    if (offset >= first && offset <= last) return true;
  }
  return false;
}
}

void resetRegisterBlock(RegisterBlock &block, uint32_t first) {
  std::fill(block.values.begin() + first, block.values.end(), 0);
  for (const auto &[offset, reset] : block.resets) {
    if (offset >= first) block.values[offset] = reset;
  }
}

void storeRegisterByte(RegisterBlock &block, uint32_t offset, uint8_t data) {
  block.values[offset] = data;
  for (const auto &[clearOffset, bits] : block.selfClearing) {
    if (clearOffset == offset) block.values[offset] &= static_cast<uint8_t>(~bits);
  }
}

int readRegisterBytes(RegisterBlock &block, uint32_t offset, int width, uint32_t *value, RegisterByteRead read) {
  uint32_t word = 0;
  for (int i = 0; i < width; ++i) word |= static_cast<uint32_t>(read(block, offset + static_cast<uint32_t>(i))) << (8 * i);
  *value = word;
  return 0;
}

int writeRegisterBytes(RegisterBlock &block, uint32_t offset, int width, uint32_t value, RegisterByteWrite write) {
  for (int i = 0; i < width; ++i) {
    uint32_t byte = offset + static_cast<uint32_t>(i);
    if (!isReadOnly(block, byte)) write(block, byte, static_cast<uint8_t>(value >> (8 * i)));
  }
  return 0;
}

void VideoInterruptRegisters::reset() {
  osdPointers = {};
  osdLowByte = {};
  interrupts = sample = 0;
}

bool VideoInterruptRegisters::read(uint32_t offset, uint8_t *value) {
  int shift = offset == interruptStatusOffset ? 16 : offset == 0x62 ? 8 : offset == 0x63 ? 0 : -1;
  if (shift < 0) return false;
  if (!sampleOnStatusRead) {
    *value = static_cast<uint8_t>(interrupts >> shift);
    interrupts &= ~(0xffU << shift);
    return true;
  }
  if (offset == interruptStatusOffset) {
    sample = interrupts;
    interrupts = 0;
  }
  *value = static_cast<uint8_t>(sample >> shift);
  return true;
}

bool VideoInterruptRegisters::write(uint32_t offset, uint8_t data) {
  if (offset != osdTopOffset && offset != osdTopOffset + 1) return false;
  std::size_t field = offset - osdTopOffset;
  uint16_t &pointer = osdPointers[field];
  pointer = osdLowByte[field] ? static_cast<uint16_t>((pointer & 0xff00) | data)
                              : static_cast<uint16_t>((pointer & 0x00ff) | data << 8);
  osdLowByte[field] = !osdLowByte[field];
  return true;
}

uint32_t VideoInterruptRegisters::osdPointer(int field) const {
  return osdPointers[field ? 1 : 0];
}

void VideoInterruptRegisters::raise(uint32_t events) {
  interrupts |= events & 0xffffffU;
}

bool VideoInterruptRegisters::line(const RegisterBlock &video) const {
  uint32_t mask = static_cast<uint32_t>(video.values[interruptMaskOffset]) << 16 |
                  static_cast<uint32_t>(video.values[0x60]) << 8 | video.values[0x61];
  return (interrupts & mask) != 0;
}
