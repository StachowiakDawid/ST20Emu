#include "STi5512_Devices.h"

#include "../../core/cpu/st20.h"
#include "../../core/memory/memory.h"
#include "STi5512_Display.h"
#include "STi5512_PtiDma.h"
#include "STi5512_RegisterBlocks.h"
#include "STi5512_VideoDecoder.h"

#include <array>
#include <utility>

/*
 * STi5512 Register Manual (7145562 B): ASC (chapter 3),
 * INC (Table 13) and INL (Table 14, chapter 14). Interrupt numbers are from the
 * STi5512 datasheet (7145560 B) Table 50: SSC0 = 5, SSC1 = 6, ASC3 = 7, ASC2 = 8, ASC1 = 9,
 * ASC0 = 10, PWM and capture = 11, video decoder = 21.
 */
namespace {
constexpr uint32_t levelBase = 0x20011000U;
constexpr uint32_t levelSources = 23;
constexpr uint32_t videoSource = 21;
Sti55xxPeripherals peripherals{false};
std::array<uint32_t, 0x80 / 4> levels{};
uint8_t videoRevision = 0;
bool initialized = false;

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
  acknowledgeInc(peripherals.controller);
}

int registerDevices() {
  std::vector<MemoryDevice> devices = sti5512RegisterBlockDevices();
  devices.push_back(sti5512PtiDmaDevice());
  devices.push_back({levelBase, levelBase + 0x7f, nullptr, readLevel, writeLevel});
  return peripherals.registerDevices(std::move(devices));
}

int resetDevices() {
  resetSti5512RegisterBlocks(videoRevision);
  resetSti5512PtiDma();
  resetSti5512Display();
  resetSti5512VideoDecoder();
  peripherals.reset();
  levels = {};
  return 0;
}
}

int pollSti5512Devices() {
  if (!initialized) return 0;
  uint32_t inputs = peripherals.tickSerial();
  tickSti5512PtiDma();
  inputs |= peripherals.tickBoardAndPwm();
  if (tickSti5512Display()) inputs |= 1U << videoSource;
  levels[0x7c / 4] = inputs;
  uint32_t routedLevels = 0;
  for (uint32_t source = 0; source < levelSources; ++source) {
    if (inputs & (1U << source)) routedLevels |= 1U << levels[source];
  }
  return pollInc(peripherals.controller, routedLevels);
}

int initSti5512Devices(uint8_t revision) {
  if (!initialized) {
    int result = registerDevices();
    if (result) return result;
  }
  videoRevision = revision;
  resetDevices();
  initialized = true;
  peripherals.attach(pollSti5512Devices, acknowledgeInterrupt, resetDevices);
  return 0;
}

void shutdownSti5512Devices() {
  if (!initialized) return;
  peripherals.detach();
  initialized = false;
}

Sti55xxPeripherals &sti5512Peripherals() {
  return peripherals;
}
