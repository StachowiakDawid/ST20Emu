#include "STi5518_Irb.h"
#include "../../core/cpu/omr.h"
#include "../common/STi55xx_Asc.h"

#include <algorithm>
#include <array>
#include <deque>
#include <utility>

/*
 * STi5518 Register Manual (7170180A) chapter 13. Only the RC-IR receiver is modelled: received
 * pulses enter a four-entry FIFO of (IRB_RX_ON_TIME, IRB_RX_SYM_PERIOD) pairs while
 * IRB_RX_EN_IR is set; reading IRB_RX_SYM_PERIOD_IR removes the pair. Times count at the
 * sampling clock, the system clock divided by IRB_RX_SAMPLING_RATE_COMMON and by 10 (1 us
 * when the divisor matches the clock as the manual asks; 2 us with this firmware's 12).
 * IRB_RX_INT_STATUS_IR reports RxIrqStatus, the last-symbol flag (while the frame's last pair
 * is the only one left), overrun and the FIFO level; the interrupt follows
 * RxIrqEnable and the selected FIFO level or the enabled last-symbol flag. The transmitter
 * and the UHF receiver are plain storage.
 */
namespace {
constexpr uint32_t base = 0x2000a200U;
constexpr uint32_t onTime = 0x40;
constexpr uint32_t symbolPeriod = 0x44;
constexpr uint32_t interruptEnable = 0x48;
constexpr uint32_t interruptStatus = 0x4c;
constexpr uint32_t receiveEnable = 0x50;
constexpr uint32_t clearOverrun = 0x58;
constexpr uint32_t samplingRate = 0x64;
constexpr std::size_t fifoDepth = 4;

std::array<uint32_t, 0xa0 / 4> registers{};
std::deque<std::pair<uint16_t, uint16_t>> fifo;
bool lastSymbolQueued = false; // the frame's last pair is in the FIFO
bool overrun = false;

/* the receiver sees the end of the frame after the last pulse's symbol time-out, by when the
 * pairs before it have been read */
bool lastSymbolPending() {
  return lastSymbolQueued && fifo.size() == 1;
}

uint32_t level() {
  return static_cast<uint32_t>(std::min<std::size_t>(fifo.size(), 3));
}

bool fifoInterrupt() {
  uint32_t threshold = (registers[interruptEnable / 4] >> 4) & 3U;
  return !fifo.empty() && level() >= std::max(threshold, 1U);
}

uint32_t status() {
  uint32_t enable = registers[interruptEnable / 4];
  bool active = (enable & 1U) && (fifoInterrupt() || (lastSymbolPending() && (enable & 2U)) || overrun);
  return (active ? 1U : 0U) | (lastSymbolPending() ? 2U : 0U) | (overrun ? 4U : 0U) | level() << 4;
}

uint16_t ticks(uint32_t microseconds) {
  uint32_t divisor = std::max(registers[samplingRate / 4] & 0xfU, 1U);
  uint64_t value = static_cast<uint64_t>(microseconds) * CPU_CLOCK / divisor / 10 / 1000000;
  return static_cast<uint16_t>(std::min<uint64_t>(value, 0xffff));
}

int readIrb(void *, uint32_t address, int width, uint32_t *value) {
  int result = checkWordAccess(address, width);
  if (result) return result;
  uint32_t offset = address - base;
  if (offset == onTime) *value = fifo.empty() ? 0 : fifo.front().first;
  else if (offset == symbolPeriod) {
    *value = fifo.empty() ? 0 : fifo.front().second;
    if (!fifo.empty()) fifo.pop_front();
    if (fifo.empty()) lastSymbolQueued = false;
  } else if (offset == interruptStatus) *value = status();
  else *value = registers[offset / 4];
  return 0;
}

int writeIrb(void *, uint32_t address, int width, uint32_t value) {
  int result = checkWordAccess(address, width);
  if (result) return result;
  uint32_t offset = address - base;
  if (offset == interruptStatus || offset == onTime || offset == symbolPeriod) return DEVICE_ACCESS_UNSUPPORTED;
  if (offset == clearOverrun) {
    if (value & 1U) overrun = false;
    return 0;
  }
  registers[offset / 4] = value;
  return 0;
}
}

MemoryDevice sti5518IrbDevice() {
  return {base, base + 0x9f, nullptr, readIrb, writeIrb};
}

void resetSti5518Irb() {
  registers = {};
  fifo.clear();
  lastSymbolQueued = overrun = false;
}

void sti5518IrbReceive(uint32_t onMicroseconds, uint32_t periodMicroseconds, bool last) {
  if (!(registers[receiveEnable / 4] & 1U) || overrun) return;
  if (fifo.size() >= fifoDepth) {
    overrun = true;
    return;
  }
  fifo.emplace_back(ticks(onMicroseconds), ticks(periodMicroseconds));
  if (last) lastSymbolQueued = true;
}

bool sti5518IrbInterruptLine() {
  return (status() & 1U) != 0;
}
