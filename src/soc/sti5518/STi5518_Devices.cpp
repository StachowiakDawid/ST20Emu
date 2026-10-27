#include "STi5518_Devices.h"
#include "../../core/cpu/st20.h"
#include "../../core/memory/memory.h"
#include "../common/STi55xx_Asc.h"
#include "../common/STi55xx_Inc.h"

#include <array>
#include <utility>

namespace {
std::array<AscDevice, 2> ascDevices{{{0x20004000U, 9}, {0x20005000U, 8}}};
constexpr uint32_t controllerBase = 0x20000000U;
constexpr uint32_t routingBase = 0x20011000U;
InterruptController controller{controllerBase, true};
std::array<uint32_t, 0x1f0 / 4> routing{};
bool initialized = false;

int readRouting(void *, uint32_t address, int width, uint32_t *value) {
  int result = checkWordAccess(address, width);
  if (result) return result;
  *value = routing[(address - routingBase) / 4];
  return 0;
}

int writeRouting(void *, uint32_t address, int width, uint32_t value) {
  int result = checkWordAccess(address, width);
  if (result) return result;
  uint32_t offset = address - routingBase;
  if (offset == 0x1b8) routing[0x1bc / 4] |= value & 0x7fffffffU;
  else if (offset == 0x1b4) routing[0x1bc / 4] &= ~value;
  else routing[offset / 4] = offset < 0x7c || (offset >= 0x100 && offset < 0x17c) ? value & 7U : value;
  return 0;
}

void acknowledgeInterrupt() {
  acknowledgeInc(controller);
}

int resetDevices() {
  for (auto &device : ascDevices) resetAsc(device);
  resetInc(controller);
  routing = {};
  routing[0x1bc / 4] = 0x7fffffffU;
  for (uint32_t source = 0; source < 31; ++source) routing[0x100 / 4 + source] = 1;
  return 0;
}
}

int pollSti5518Devices() {
  if (!initialized) return 0;
  uint32_t rawSources = 0;
  uint32_t routedLevels = 0;
  for (auto &device : ascDevices) {
    bool source = tickAsc(device);
    if (source) rawSources |= 1U << device.source;
    bool enabledSource = (routing[0x1bc / 4] & (1U << device.source)) != 0;
    bool routed = enabledSource && triggered(routing[0x100 / 4 + device.source], source, device.previousSource);
    device.previousSource = source;
    if (routed) routedLevels |= 1U << routing[device.source];
  }
  routing[0x7c / 4] = rawSources;
  return pollInc(controller, routedLevels);
}

int initSti5518Devices() {
  if (!initialized) {
    std::size_t registered = 0;
    for (auto &device : ascDevices) {
      int result = registerMemoryDevice({device.base, device.base + ascRegisterEnd, &device, readAsc, writeAsc});
      if (result) {
        for (std::size_t index = 0; index < registered; ++index) unregisterMemoryDevice(ascDevices[index].base);
        return result;
      }
      ++registered;
    }
    int result = registerMemoryDevice({controllerBase, controllerBase + incRegisterEnd, &controller, readInc, writeInc});
    if (result) {
      for (const auto &device : ascDevices) unregisterMemoryDevice(device.base);
      return result;
    }
    result = registerMemoryDevice({routingBase, routingBase + 0x1ef, nullptr, readRouting, writeRouting});
    if (result) {
      for (const auto &device : ascDevices) unregisterMemoryDevice(device.base);
      unregisterMemoryDevice(controllerBase);
      return result;
    }
  }
  resetDevices();
  for (auto &device : ascDevices) device.transmitted.clear();
  initialized = true;
  setPeripheralPoller(pollSti5518Devices);
  setInterruptReturnObserver(acknowledgeInterrupt);
  setRebootObserver(resetDevices);
  return 0;
}

void shutdownSti5518Devices() {
  if (!initialized) return;
  for (const auto &device : ascDevices) unregisterMemoryDevice(device.base);
  unregisterMemoryDevice(controllerBase);
  unregisterMemoryDevice(routingBase);
  setPeripheralPoller(nullptr);
  setInterruptReturnObserver(nullptr);
  setRebootObserver(nullptr);
  initialized = false;
}

std::vector<uint8_t> takeSti5518Asc1Transmit() {
  return std::exchange(ascDevices[0].transmitted, {});
}

std::vector<uint8_t> takeSti5518Asc2Transmit() {
  return std::exchange(ascDevices[1].transmitted, {});
}