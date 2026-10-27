#include "STi5512_Devices.h"

#include "../../core/cpu/st20.h"
#include "../../core/memory/memory.h"
#include "../common/STi55xx_Asc.h"
#include "../common/STi55xx_Inc.h"
#include "../common/STi55xx_Ssc.h"
#include "STi5512_Display.h"
#include "STi5512_Pio.h"
#include "STi5512_PtiDma.h"
#include "STi5512_Pwm.h"
#include "STi5512_RegisterBlocks.h"
#include "STi5512_VideoDecoder.h"

#include <array>
#include <bit>
#include <utility>

/*
 * STi5512 Register Manual (7145562 B): ASC (chapter 3),
 * INC (Table 13) and INL (Table 14, chapter 14). Interrupt numbers are from the
 * STi5512 datasheet (7145560 B) Table 50: SSC0 = 5, SSC1 = 6, ASC3 = 7, ASC2 = 8, ASC1 = 9,
 * ASC0 = 10, PWM and capture = 11, video decoder = 21.
 */
namespace {
constexpr uint32_t controllerBase = 0x20000000U;
constexpr uint32_t levelBase = 0x20011000U;
constexpr uint32_t levelSources = 23;
constexpr uint32_t pwmSource = 11;
constexpr uint32_t videoSource = 21;
BoardPoller boardPoller = nullptr;
std::array<AscDevice, 4> ascDevices{{{0x20003000U, 10}, {0x20004000U, 9}, {0x20005000U, 8}, {0x20006000U, 7}}};
std::array<SscDevice, 2> sscDevices{{{0x20009000U, 5}, {0x2000a000U, 6}}};
std::array<uint32_t, 0x10c / 4> controller{};
std::array<uint32_t, 0x80 / 4> levels{};
std::array<bool, 8> previousLevels{};
uint8_t videoRevision = 0;
bool initialized = false;
uint32_t softwarePending = 0;
uint32_t hardwarePending = 0;

int readController(void *, uint32_t address, int width, uint32_t *value) {
  int result = checkWordAccess(address, width);
  if (result) return result;
  *value = controller[(address - controllerBase) / 4];
  return 0;
}

/* Mask has 8 IntEn bits plus GloEn (bit 16); Pending and Exec have one bit per level. */
int writeController(void *, uint32_t address, int width, uint32_t value) {
  int result = checkWordAccess(address, width);
  if (result) return result;
  uint32_t offset = address - controllerBase;
  uint32_t bits = value & 0xffU;
  uint32_t maskBits = value & 0x100ffU;
  if (offset == 0xc4) controller[0xc0 / 4] |= maskBits;
  else if (offset == 0xc8) controller[0xc0 / 4] &= ~maskBits;
  else if (offset == 0xc0) controller[0xc0 / 4] = maskBits;
  else if (offset == 0x84) softwarePending |= bits;
  else if (offset == 0x88) { softwarePending &= ~bits; hardwarePending &= ~bits; }
  else if (offset == 0x80) { softwarePending = bits; hardwarePending &= bits; }
  else if (offset == 0x104) controller[0x100 / 4] |= bits;
  else if (offset == 0x108) controller[0x100 / 4] &= ~bits;
  else if (offset == 0x100) controller[0x100 / 4] = bits;
  else if (offset >= 0x40 && offset < 0x60) controller[offset / 4] = value & 7U;
  else controller[offset / 4] = value;
  controller[0x80 / 4] = softwarePending | hardwarePending;
  return 0;
}

int readLevel(void *, uint32_t address, int width, uint32_t *value) {
  int result = checkWordAccess(address, width);
  if (result) return result;
  *value = levels[(address - levelBase) / 4];
  return 0;
}

int writeLevel(void *, uint32_t address, int width, uint32_t value) {
  int result = checkWordAccess(address, width);
  if (result) return result;
  uint32_t offset = address - levelBase;
  if (offset == 0x7c) return DEVICE_ACCESS_UNSUPPORTED;
  if (offset < levelSources * 4) levels[offset / 4] = value & 7U;
  else if (offset == 0x60 || offset == 0x64) levels[offset / 4] = value & 0xfU;
  else levels[offset / 4] = value;
  return 0;
}

void acknowledgeInterrupt() {
  uint32_t &executing = controller[0x100 / 4];
  if (executing) {
    int level = std::bit_width(executing) - 1;
    if (level > activeExternalInterruptLevel()) executing &= ~(1U << level);
  }
}

std::vector<uint32_t> registeredBases;

void unregisterDevices() {
  for (uint32_t base : registeredBases) unregisterMemoryDevice(base);
  registeredBases.clear();
}

int registerDevices() {
  std::vector<MemoryDevice> devices = sti5512RegisterBlockDevices();
  for (auto &device : ascDevices) devices.push_back({device.base, device.base + ascRegisterEnd, &device, readAsc, writeAsc});
  for (auto &device : sscDevices) devices.push_back({device.base, device.base + sscRegisterEnd, &device, readSsc, writeSsc});
  devices.push_back(sti5512PtiDmaDevice());
  devices.push_back(sti5512PwmDevice());
  for (const auto &device : sti5512PioDevices()) devices.push_back(device);
  devices.push_back({controllerBase, controllerBase + 0x10b, nullptr, readController, writeController});
  devices.push_back({levelBase, levelBase + 0x7f, nullptr, readLevel, writeLevel});
  for (const auto &device : devices) {
    int result = registerMemoryDevice(device);
    if (result) {
      unregisterDevices();
      return result;
    }
    registeredBases.push_back(device.start);
  }
  return 0;
}

int resetDevices() {
  resetSti5512RegisterBlocks(videoRevision);
  for (auto &device : ascDevices) resetAsc(device);
  for (auto &device : sscDevices) resetSsc(device);
  resetSti5512PtiDma();
  resetSti5512Pwm();
  resetSti5512Pio();
  resetSti5512Display();
  resetSti5512VideoDecoder();
  controller = {};
  levels = {};
  previousLevels = {};
  softwarePending = hardwarePending = 0;
  return 0;
}
}

int pollSti5512Devices() {
  if (!initialized) return 0;
  uint32_t inputs = 0;
  for (auto &device : ascDevices) {
    if (tickAsc(device)) inputs |= 1U << device.source;
  }
  for (auto &device : sscDevices) {
    if (tickSsc(device)) inputs |= 1U << device.source;
  }
  tickSti5512PtiDma();
  if (boardPoller) boardPoller();
  if (tickSti5512Pwm()) inputs |= 1U << pwmSource;
  if (tickSti5512Display()) inputs |= 1U << videoSource;
  levels[0x7c / 4] = inputs;
  uint32_t routedLevels = 0;
  for (uint32_t source = 0; source < levelSources; ++source) {
    if (inputs & (1U << source)) routedLevels |= 1U << levels[source];
  }
  for (uint32_t level = 0; level < 8; ++level) {
    bool line = (routedLevels & (1U << level)) != 0;
    uint32_t mode = controller[0x40 / 4 + level] & 7U;
    uint32_t bit = 1U << level;
    if (mode == 1 || mode == 2) hardwarePending &= ~bit;
    if (triggered(mode, line, previousLevels[level])) hardwarePending |= bit;
    previousLevels[level] = line;
  }
  controller[0x80 / 4] = hardwarePending | softwarePending;
  uint32_t mask = controller[0xc0 / 4];
  uint32_t enabled = mask & 0x10000U ? mask & 0xffU : 0;
  for (int level = 7; level >= 0; --level) {
    uint32_t bit = 1U << level;
    if (!(controller[0x80 / 4] & enabled & bit) || (controller[0x100 / 4] & bit)) continue;
    if (controller[static_cast<std::size_t>(level)] == 0) continue;
    bool accepted = false;
    int result = serviceNativeInterrupt(static_cast<uint32_t>(level), controller[static_cast<std::size_t>(level)], &accepted);
    if (result) return result;
    if (accepted) {
      softwarePending &= ~bit;
      hardwarePending &= ~bit;
      controller[0x80 / 4] &= ~bit;
      controller[0x100 / 4] |= bit;
      break;
    }
  }
  return 0;
}

int initSti5512Devices(uint8_t revision) {
  if (!initialized) {
    int result = registerDevices();
    if (result) return result;
  }
  videoRevision = revision;
  resetDevices();
  for (auto &device : ascDevices) device.transmitted.clear();
  initialized = true;
  setPeripheralPoller(pollSti5512Devices);
  setInterruptReturnObserver(acknowledgeInterrupt);
  setRebootObserver(resetDevices);
  return 0;
}

void shutdownSti5512Devices() {
  if (!initialized) return;
  unregisterDevices();
  setPeripheralPoller(nullptr);
  setInterruptReturnObserver(nullptr);
  setRebootObserver(nullptr);
  setSti5512PioObserver(nullptr);
  setSti5512PioInputSource(nullptr);
  boardPoller = nullptr;
  for (auto &device : sscDevices) device.slaves.clear();
  initialized = false;
}

std::vector<uint8_t> takeSti5512AscTransmit(int port) {
  if (port < 0 || port >= static_cast<int>(ascDevices.size())) return {};
  return std::exchange(ascDevices[static_cast<std::size_t>(port)].transmitted, {});
}

int attachSti5512I2cDevice(int port, I2cRegisterDevice *device) {
  if (port < 0 || port >= static_cast<int>(sscDevices.size()) || device == nullptr) return INVALID_MEMORY_DEVICE;
  sscDevices[static_cast<std::size_t>(port)].slaves.push_back(device);
  return 0;
}

void setSti5512BoardPoller(BoardPoller poller) {
  boardPoller = poller;
}
