#include "STi5512_PtiDma.h"
#include "STi5512_AudioDecoder.h"
#include "STi5512_VideoDecoder.h"
#include "../common/STi55xx_Asc.h"
#include "../../core/cpu/st20.h"
#include "../../core/memory/memory.h"

#include <algorithm>
#include <array>

/*
 * STi5512 Register Manual (7145562 B), section 20.1. An enabled channel 1-3 moves its read
 * pointer towards the write pointer, wrapping to base after the last byte of the 16-byte
 * block that top addresses (the drivers write top = base + size - 16, the manual top | 0xf;
 * both mean the same byte). Channel 1 delivers the bytes to the video decoder and channel 2
 * to the audio decoder, each stopping while its input is full; channel 3 (sub-picture)
 * discards its data. The transport controller model (STi5512_Pti.cpp) writes the circular
 * buffers and advances the write pointers of channels 1-3, as channel 0 does in hardware.
 */
namespace {
constexpr uint32_t dmaBase = 0x20031000U;
constexpr uint32_t dmaEnd = 0x20031063U;
constexpr uint32_t baseRegister = 0x00 / 4;
constexpr uint32_t topRegister = 0x10 / 4;
constexpr uint32_t writeRegister = 0x20 / 4;
constexpr uint32_t readRegister = 0x30 / 4;
constexpr uint32_t enableRegister = 0x50 / 4;
/* devices are polled every few dozen cycles, so the budget follows the cycles since the last
 * transfer; it is generous enough that the bit buffer, not the DMA, limits the rate */
constexpr uint64_t bytesPerCycle = 64;
constexpr uint64_t maxBytesPerTick = 64 * 1024;

std::array<uint32_t, (dmaEnd + 1 - dmaBase) / 4> dma{};
uint64_t lastCycle = 0;

int readDma(void *, uint32_t address, int width, uint32_t *value) {
  int result = checkWordAccess(address, width);
  if (result) return result;
  *value = dma[(address - dmaBase) / 4];
  return 0;
}

int writeDma(void *, uint32_t address, int width, uint32_t value) {
  int result = checkWordAccess(address, width);
  if (result) return result;
  dma[(address - dmaBase) / 4] = value;
  return 0;
}
}

MemoryDevice sti5512PtiDmaDevice() {
  return {dmaBase, dmaEnd, nullptr, readDma, writeDma};
}

void resetSti5512PtiDma() {
  dma = {};
  lastCycle = elapsedCpuCycles();
}

void tickSti5512PtiDma() {
  uint64_t now = elapsedCpuCycles();
  uint64_t bytesPerTick = std::min(std::max<uint64_t>(now - lastCycle, 1) * bytesPerCycle, maxBytesPerTick);
  lastCycle = now;
  for (uint32_t channel = 1; channel < 4; ++channel) {
    if (!(dma[enableRegister] & (1U << channel))) continue;
    uint32_t &read = dma[readRegister + channel];
    uint32_t write = dma[writeRegister + channel];
    for (uint32_t count = 0; count < bytesPerTick && read != write; ++count) {
      if (channel == 1 || channel == 2) {
        uint32_t byte = 0;
        readBytes(read, 1, &byte);
        bool accepted = channel == 1 ? sti5512VideoInput(static_cast<uint8_t>(byte))
                                     : sti5512AudioInput(static_cast<uint8_t>(byte));
        if (!accepted) break;
      }
      read = read == (dma[topRegister + channel] | 0xfU) ? dma[baseRegister + channel] : read + 1;
    }
  }
}

uint32_t sti5512PtiDmaRegister(uint32_t offset) {
  return offset < dmaEnd + 1 - dmaBase ? dma[offset / 4] : 0;
}

void sti5512PtiDmaSetWrite(uint32_t channel, uint32_t address) {
  if (channel < 4) dma[writeRegister + channel] = address;
}
