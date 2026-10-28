#include "STi5518_RegisterBlocks.h"
#include "../common/STi55xx_RegisterBlocks.h"

#include <array>

/*
 * Reset values and read-only registers are from the STi5518 Register Manual (7170180A);
 * the block windows are the datasheet memory map (7170179 D, Table 35). Each block covers its
 * whole window, so reserved locations are storage too. Registers are plain storage: writes to
 * read-only registers are ignored, reads return the last written or reset value. The video
 * decoder registers sit at the STi5512 offsets: OSD_OTP and OSD_OBP (0x2a, 0x2b) are serial
 * (high byte, then low byte); VID_ITS latches the events raised by the display model and each
 * of its bytes (0x3d, 0x62, 0x63) clears when read. The manual only says that reading the most
 * significant byte clears it; the 6300 firmware's handler reads 0x3d and then expects the
 * VSYNC bits at 0x63.
 * VID_STA reads as an idle decoder with empty buffers. There is no audio decoding, so AUD_INT
 * stays 0; an AUD_SOFTRESET write with bit 0 clears AUD_INTE, AUD_PLAY and AUD_MUTE. Writes to the
 * compressed-data FIFOs (0x800-0xdff) are discarded and their reads return 0.
 *
 * The MPEG block decodes 12 address bits: 0x1000-0x1fff mirrors it. The boot code programs
 * the video registers at 0x000, the application at 0x1000 (a video base that is not null).
 */
namespace {
constexpr uint32_t audioInterruptEnable = 0x07;   // AUD_INTE, 16 bits
constexpr uint32_t audioInterrupt = 0x09;         // AUD_INT, 16 bits
constexpr uint32_t audioSoftReset = 0x10;         // AUD_SOFTRESET

std::array<RegisterBlock, 6> blocks{{
    // VideoBaseAddress 0x000: VID, OSD, PES, USD, BCK; Clock GeneratorBaseAddress 0x100
    {0x000, 0x200,
     {{0x43, 0x02},   // PES_TM2
      {0x64, 0x23},   // VID_STA2: BMI, DEI, PII
      {0x65, 0x1c},   // VID_STA3: BBE, BBF, HFE
      {0x99, 0xff}},  // VID_BCK_V
     {{0x16, 0x17},   // VID_VBL
      {0x1e, 0x1f},   // VID_ABL
      {0x3b, 0x3b},   // VID_STA1
      {0x3d, 0x3d},   // VID_ITS
      {0x42, 0x43},   // PES_TM1, PES_TM2
      {0x49, 0x4d},   // PES_TS
      {0x62, 0x65},   // VID_ITS, VID_STA
      {0x66, 0x69},   // VID_HDF, VID_CDCOUNT, VID_SCDCOUNT
      {0x78, 0x78},   // VID_REV
      {0x81, 0x85},   // VID_TP_SCD_RD, VID_TP_LDP
      {0xc3, 0xc5},   // VID_TP_VLD_RD
      {0xcd, 0xcf}}}, // VID_TP_SCD_CURRENT
    // AudioBaseAddress 0x200
    {0x200, 0x200,
     {{0x00, 0x20},   // AUD_VERSION
      {0x01, 0xac},   // AUD_IDENT
      {0x12, 0x01},   // AUD_PLLPCM
      {0x71, 0x47}},  // AUD_SOFTVER
     {{0x00, 0x01},   // AUD_VERSION, AUD_IDENT
      {0x09, 0x0a},   // AUD_INT
      {0x0f, 0x0f},   // AUD_ERROR
      {0x40, 0x44},   // AUD_SYNC_STATUS, AUD_ANCCOUNT, AUD_HEAD4-3, AUD_HEADLEN
      {0x56, 0x56},   // AUD_PCMCROSS
      {0x76, 0x7d}}}, // AUD_*_STATUS0-7
    // SubPictureBaseAddress 0x400
    {0x400, 0x200, {}, {}},
    // DencBaseAddress 0x600 (0x700-0x7ff reserved)
    {0x600, 0x200,
     {{0x00, 0x92}, {0x01, 0x44}, {0x02, 0x20}, {0x06, 0x10}, {0x08, 0x20}, // DEN_CFG0-8
      {0x11, 0x88}, {0x12, 0x88}, {0x13, 0x80},                             // DEN_DAC13-DAC6C
      {0x2b, 0x0f}, {0x2c, 0x0f},                                           // DEN_CLF1-2
      {0x40, 0x50}, {0x41, 0x82}, {0x45, 0x80}, {0x47, 0x80}},              // DEN_REG_64-71
     {{0x09, 0x09},   // DEN_STA
      {0x18, 0x18}}}, // DEN_CID
    // video, audio and sub-picture compressed-data FIFO accesses
    {0x800, 0x600, {}, {}},
    // MPEG_CONTROL 0xe00
    {0xe00, 0x200, {}, {}},
}};

enum BlockIndex { videoBlock, audioBlock, subPictureBlock, dencBlock, fifoBlock, controlBlock };

VideoInterruptRegisters video{false};

uint8_t readByte(RegisterBlock &block, uint32_t offset) {
  uint8_t status = 0;
  if (&block == &blocks[videoBlock] && video.read(offset, &status)) return status;
  return &block == &blocks[fifoBlock] ? 0 : block.values[offset];
}

void writeByte(RegisterBlock &block, uint32_t offset, uint8_t data) {
  if (&block == &blocks[fifoBlock]) return;
  if (&block == &blocks[videoBlock] && video.write(offset, data)) return;
  if (&block == &blocks[audioBlock] && offset == audioSoftReset) {
    if (data & 1U) {
      for (uint32_t reset = audioInterruptEnable; reset < audioInterrupt + 2; ++reset) block.values[reset] = 0;
      for (uint32_t command = 0x13; command <= 0x14; ++command) block.values[command] = 0;
    }
    return;
  }
  storeRegisterByte(block, offset, data);
}

int readBlock(void *context, uint32_t address, int width, uint32_t *value) {
  auto &block = *static_cast<RegisterBlock *>(context);
  return readRegisterBytes(block, address - block.base, width, value, readByte);
}

int writeBlock(void *context, uint32_t address, int width, uint32_t value) {
  auto &block = *static_cast<RegisterBlock *>(context);
  return writeRegisterBytes(block, address - block.base, width, value, writeByte);
}

constexpr uint32_t mirrorBase = 0x1000;

RegisterBlock &blockAt(uint32_t offset) {
  for (auto &block : blocks) {
    if (offset >= block.base && offset < block.base + block.size) return block;
  }
  return blocks[controlBlock];
}

/* an access stays within one block: every block is word aligned and a multiple of 4 bytes */
int readMirror(void *, uint32_t address, int width, uint32_t *value) {
  uint32_t offset = address - mirrorBase;
  return readBlock(&blockAt(offset), offset, width, value);
}

int writeMirror(void *, uint32_t address, int width, uint32_t value) {
  uint32_t offset = address - mirrorBase;
  return writeBlock(&blockAt(offset), offset, width, value);
}
}

std::vector<MemoryDevice> sti5518RegisterBlockDevices() {
  std::vector<MemoryDevice> devices;
  for (auto &block : blocks) devices.push_back({block.base, block.base + block.size - 1, &block, readBlock, writeBlock});
  devices.push_back({mirrorBase, mirrorBase + 0xfff, nullptr, readMirror, writeMirror});
  return devices;
}

void resetSti5518RegisterBlocks() {
  for (auto &block : blocks) {
    block.values.assign(block.size, 0);
    resetRegisterBlock(block);
  }
  video.reset();
}

uint8_t sti5518VideoRegister(uint32_t offset) {
  return offset < blocks[videoBlock].size ? blocks[videoBlock].values[offset] : 0;
}

uint32_t sti5518OsdPointer(int field) {
  return video.osdPointer(field);
}

void raiseSti5518VideoInterrupt(uint32_t events) {
  video.raise(events);
}

bool sti5518VideoInterruptLine() {
  return video.line(blocks[videoBlock]);
}

bool sti5518AudioInterruptLine() {
  const auto &audio = blocks[audioBlock].values;
  uint32_t enable = audio[audioInterruptEnable] | static_cast<uint32_t>(audio[audioInterruptEnable + 1]) << 8;
  uint32_t pending = audio[audioInterrupt] | static_cast<uint32_t>(audio[audioInterrupt + 1]) << 8;
  return (enable & pending) != 0;
}
