#include "STi5512_RegisterBlocks.h"

#include <algorithm>
#include <array>
#include <utility>

/*
 * Reset values and read-only registers are from the STi5512 Register Manual (7145562 B).
 * Each block covers its whole window in the datasheet memory map (7145560 B, Table 51), so
 * reserved locations are storage too (the manual leaves their read value undefined).
 * Registers are plain storage: writes to read-only registers are ignored, reads return
 * the last written or reset value. Command bits the manual describes as resetting
 * automatically read back as 0, and the audio and DENC soft resets restore their reset
 * values. Other hardware side effects (decoding, serial pointers, clear-on-read status) are
 * not modelled; undefined reset values read as 0.
 */
namespace {
struct RegisterBlock {
  uint32_t base;
  uint32_t size;
  std::vector<std::pair<uint32_t, uint8_t>> resets;
  std::vector<std::pair<uint32_t, uint32_t>> readOnly; // inclusive offset ranges
  std::vector<std::pair<uint32_t, uint8_t>> selfClearing{}; // offset, bits reset automatically
  std::vector<uint8_t> values{};
};

constexpr uint32_t videoRevisionOffset = 0x78;
constexpr uint32_t audioResetOffset = 0x40;          // AUD_RES
constexpr uint32_t dencSoftResetOffset = 0x06;       // DEN_CFG6
constexpr uint8_t dencSoftResetBit = 0x80;
constexpr uint32_t dencUserRegisters = 9;            // DEN_CFG0-8 survive a DENC soft reset

std::array<RegisterBlock, 7> blocks{{
    // VideoBaseAddress 0x0000: CFG, CKG, VID, OSD, PES, TDL, USD and BCK registers
    {0x0000, 0x800,
     {{0x30, 0x80},   // CKG_PLL: PLL in power down mode
      {0x43, 0x02},   // PES_TM2
      {0x64, 0x23},   // VID_STA: BMI, DEI, PII
      {0x65, 0x1c},   // VID_STA: BBE, BBF, HFE
      {0x90, 0x0c},   // VID_OUT
      {0x9a, 0xff}},  // BCK_V
     {{0x16, 0x17},   // VID_VBL
      {0x1e, 0x1f},   // VID_ABL
      {0x3b, 0x3b},   // VID_STA
      {0x3d, 0x3d},   // VID_ITS
      {0x42, 0x43},   // PES_TM1, PES_TM2
      {0x49, 0x4d},   // PES_TS
      {0x62, 0x65},   // VID_ITS, VID_STA
      {0x66, 0x68},   // VID_HDF, VID_CDcount and serial status
      {0x78, 0x78}}}, // VID_REV
    // AudioBaseAddress 0x0800
    {0x0800, 0x800,
     {},
     {{0x06, 0x0f},   // AUD_ANC, AUD_ESCCO
      {0x1a, 0x1b},   // AUD_ITR
      {0x26, 0x26},   // AUD_SYS
      {0x44, 0x44},   // AUD_SFR
      {0x46, 0x46},   // AUD_DEM
      {0x5e, 0x66},   // AUD_HDR, AUD_PTS
      {0x6c, 0x6d}},  // AUD_ADA, AUD_REV
     {{0x32, 0x01},   // AUD_SKP
      {0x40, 0x01},   // AUD_RES
      {0x42, 0x01}}}, // AUD_RST
    // SubPictureBaseAddress 0x1000
    {0x1000, 0x800, {}, {}},
    // DencBaseAddress 0x1800
    {0x1800, 0x800,
     {{0x00, 0x92}, {0x01, 0x44}, {0x02, 0x20}, {0x06, 0x10}, {0x08, 0x20}, // DEN_CFG0-8
      {0x11, 0x88}, {0x12, 0x88}, {0x13, 0x80},                             // DEN_DAC13-DAC6C
      {0x2b, 0x0f}, {0x2c, 0x0f},                                           // DEN_CLF1-2
      {0x40, 0x50}, {0x41, 0x82}, {0x45, 0x80}, {0x47, 0x80}},              // DEN_REG_64-71
     {{0x09, 0x09},   // DEN_STA
      {0x18, 0x18}},  // DEN_CID
     {{0x06, 0xc0},   // DEN_CFG6 softreset, jump
      {0x08, 0xc0}}}, // DEN_CFG8 ph_rst_mode
    // EMIBaseAddress 0x2000
    {0x2000, 0x1000, {}, {{0x50, 0x50}}}, // EMI_ConfigStatus
    // CacheBaseAddress 0x4000
    {0x4000, 0x1000, {}, {{0x900, 0x900}}}, // CAC_STATUS
    // MPEG Base Address 0x5000: MPEG_control
    {0x5000, 0x4, {}, {}},
}};

enum BlockIndex { videoBlock, audioBlock, subPictureBlock, dencBlock };

void resetBlock(RegisterBlock &block, uint32_t first) {
  std::fill(block.values.begin() + first, block.values.end(), 0);
  for (const auto &[offset, reset] : block.resets) {
    if (offset >= first) block.values[offset] = reset;
  }
}

/* audio-only reset, except AUD_MUT, AUD_PLY and AUD_SCP */
void resetAudio(RegisterBlock &block) {
  uint8_t mute = block.values[0x30];
  uint8_t play = block.values[0x2e];
  uint8_t sclk = block.values[0x53];
  resetBlock(block, 0);
  block.values[0x30] = mute;
  block.values[0x2e] = play;
  block.values[0x53] = sclk;
}

bool isReadOnly(const RegisterBlock &block, uint32_t offset) {
  for (const auto &[first, last] : block.readOnly) {
    if (offset >= first && offset <= last) return true;
  }
  return false;
}

int readBlock(void *context, uint32_t address, int width, uint32_t *value) {
  auto &block = *static_cast<RegisterBlock *>(context);
  uint32_t offset = address - block.base;
  uint32_t word = 0;
  for (int i = 0; i < width; ++i) word |= static_cast<uint32_t>(block.values[offset + static_cast<uint32_t>(i)]) << (8 * i);
  *value = word;
  return 0;
}

int writeBlock(void *context, uint32_t address, int width, uint32_t value) {
  auto &block = *static_cast<RegisterBlock *>(context);
  uint32_t offset = address - block.base;
  for (int i = 0; i < width; ++i) {
    uint32_t byte = offset + static_cast<uint32_t>(i);
    if (isReadOnly(block, byte)) continue;
    uint8_t data = static_cast<uint8_t>(value >> (8 * i));
    if (&block == &blocks[audioBlock] && byte == audioResetOffset) resetAudio(block);
    if (&block == &blocks[dencBlock] && byte == dencSoftResetOffset && (data & dencSoftResetBit))
      resetBlock(block, dencUserRegisters);
    block.values[byte] = data;
    for (const auto &[clearOffset, bits] : block.selfClearing) {
      if (clearOffset == byte) block.values[byte] &= static_cast<uint8_t>(~bits);
    }
  }
  return 0;
}
}

std::vector<MemoryDevice> sti5512RegisterBlockDevices() {
  std::vector<MemoryDevice> devices;
  for (auto &block : blocks) devices.push_back({block.base, block.base + block.size - 1, &block, readBlock, writeBlock});
  return devices;
}

void resetSti5512RegisterBlocks(uint8_t videoRevision) {
  for (auto &block : blocks) {
    block.values.assign(block.size, 0);
    resetBlock(block, 0);
  }
  blocks[0].values[videoRevisionOffset] = videoRevision;
}
