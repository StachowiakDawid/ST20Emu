#include "STi5518_Devices.h"
#include "../../core/cpu/st20.h"
#include "../../core/memory/memory.h"

#include <array>
#include <bit>
#include <utility>

namespace {
struct AscDevice {
  uint32_t base;
  uint32_t source;
  std::array<uint32_t, 11> registers{};
  bool previousSource = false;
  uint32_t transmitDelay = 0;
  std::vector<uint8_t> transmitted{};
};
std::array<AscDevice, 2> ascDevices{{{0x20004000U, 9}, {0x20005000U, 8}}};
constexpr uint32_t controllerBase = 0x20000000U;
constexpr uint32_t routingBase = 0x20011000U;
std::array<uint32_t, 0x10c / 4> controller{};
std::array<uint32_t, 0x1f0 / 4> routing{};
std::array<bool, 8> previousLevels{};
bool initialized = false;
uint32_t softwarePending = 0;
uint32_t hardwarePending = 0;

bool triggered(uint32_t mode, bool current, bool previous) {
  switch (mode & 7U) {
    case 1: return current;
    case 2: return !current;
    case 3: return current && !previous;
    case 4: return !current && previous;
    case 5: return current != previous;
    default: return false;
  }
}

int checkAccess(uint32_t address, int width) {
  return width != 4 || (address & 3U) ? INVALID_DEVICE_ACCESS : 0;
}

int readAsc(void *context, uint32_t address, int width, uint32_t *value) {
  int result = checkAccess(address, width);
  if (result) return result;
  auto &device = *static_cast<AscDevice *>(context);
  auto &asc = device.registers;
  uint32_t offset = address - device.base;
  if (offset == 4 || offset == 0x20 || offset == 0x24) return DEVICE_ACCESS_UNSUPPORTED;
  *value = asc[offset / 4];
  if (offset == 8) asc[0x14 / 4] &= ~0x21U;
  return 0;
}

int writeAsc(void *context, uint32_t address, int width, uint32_t value) {
  int result = checkAccess(address, width);
  if (result) return result;
  auto &device = *static_cast<AscDevice *>(context);
  auto &asc = device.registers;
  uint32_t offset = address - device.base;
  if (offset == 8 || offset == 0x14) return DEVICE_ACCESS_UNSUPPORTED;
  if (offset == 4) {
    asc[1] = value & 0x1ffU;
    if (asc[3] & 0x80U) {
      device.transmitted.push_back(static_cast<uint8_t>(value));
      device.transmitDelay = 2;
      asc[5] &= ~6U;
    }
  } else if (offset == 0x20) {
    device.transmitDelay = 0;
    asc[5] |= 6U;
  } else if (offset == 0x24) {
    asc[2] = 0;
    asc[5] &= ~0x21U;
  } else {
    uint32_t mask = offset == 0 ? 0xffffU : offset == 0xc ? 0x1fffU : offset == 0x10 ? 0x1ffU : 0xffU;
    asc[offset / 4] = value & mask;
  }
  return 0;
}

int readController(void *, uint32_t address, int width, uint32_t *value) {
  int result = checkAccess(address, width);
  if (result) return result;
  *value = controller[(address - controllerBase) / 4];
  return 0;
}

int writeController(void *, uint32_t address, int width, uint32_t value) {
  int result = checkAccess(address, width);
  if (result) return result;
  uint32_t offset = address - controllerBase;
  uint32_t bits = value & 0x10000U ? 0xffU : value & 0xffU;
  if (offset == 0xc4) controller[0xc0 / 4] |= bits;
  else if (offset == 0xc8) controller[0xc0 / 4] &= ~bits;
  else if (offset == 0xc0) controller[0xc0 / 4] = value & 0xffU;
  else if (offset == 0x84) softwarePending |= bits;
  else if (offset == 0x88) { softwarePending &= ~bits; hardwarePending &= ~bits; }
  else if (offset == 0x80) softwarePending = value & 0xffU;
  else if (offset == 0x104) controller[0x100 / 4] |= bits;
  else if (offset == 0x108) controller[0x100 / 4] &= ~bits;
  else controller[offset / 4] = value;
  controller[0x80 / 4] = softwarePending | hardwarePending;
  return 0;
}

int readRouting(void *, uint32_t address, int width, uint32_t *value) {
  int result = checkAccess(address, width);
  if (result) return result;
  *value = routing[(address - routingBase) / 4];
  return 0;
}

int writeRouting(void *, uint32_t address, int width, uint32_t value) {
  int result = checkAccess(address, width);
  if (result) return result;
  uint32_t offset = address - routingBase;
  if (offset == 0x1b8) routing[0x1bc / 4] |= value & 0x7fffffffU;
  else if (offset == 0x1b4) routing[0x1bc / 4] &= ~value;
  else routing[offset / 4] = offset < 0x7c || (offset >= 0x100 && offset < 0x17c) ? value & 7U : value;
  return 0;
}

void acknowledgeInterrupt() {
  uint32_t &executing = controller[0x100 / 4];
  if (executing) {
    int level = std::bit_width(executing) - 1;
    if (level > activeExternalInterruptLevel()) executing &= ~(1U << level);
  }
}
}

int pollSti5518Devices() {
  if (!initialized) return 0;
  uint32_t rawSources = 0;
  uint32_t routedLevels = 0;
  for (auto &device : ascDevices) {
    auto &asc = device.registers;
    if (device.transmitDelay && --device.transmitDelay == 0) asc[5] |= 6U;
    if (device.transmitDelay == 0 && (asc[3] & 0x80U)) asc[5] |= 6U;
    bool source = (asc[3] & 0x80U) && (asc[5] & asc[4]);
    if (source) rawSources |= 1U << device.source;
    bool enabledSource = (routing[0x1bc / 4] & (1U << device.source)) != 0;
    bool routed = enabledSource && triggered(routing[0x100 / 4 + device.source], source, device.previousSource);
    device.previousSource = source;
    if (routed) routedLevels |= 1U << routing[device.source];
  }
  routing[0x7c / 4] = rawSources;
  for (uint32_t level = 0; level < 8; ++level) {
    bool line = (routedLevels & (1U << level)) != 0;
    uint32_t mode = controller[0x40 / 4 + level] & 7U;
    uint32_t bit = 1U << level;
    if (mode == 1 || mode == 2) hardwarePending &= ~bit;
    if (triggered(mode, line, previousLevels[level])) hardwarePending |= bit;
    previousLevels[level] = line;
  }
  controller[0x80 / 4] = hardwarePending | softwarePending;
  for (int level = 7; level >= 0; --level) {
    uint32_t bit = 1U << level;
    if (!(controller[0x80 / 4] & controller[0xc0 / 4] & bit) || (controller[0x100 / 4] & bit)) continue;
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

int initSti5518Devices() {
  if (!initialized) {
    std::size_t registered = 0;
    for (auto &device : ascDevices) {
      int result = registerMemoryDevice({device.base, device.base + 0x2b, &device, readAsc, writeAsc});
      if (result) {
        for (std::size_t index = 0; index < registered; ++index) unregisterMemoryDevice(ascDevices[index].base);
        return result;
      }
      ++registered;
    }
    int result = registerMemoryDevice({controllerBase, controllerBase + 0x10b, nullptr, readController, writeController});
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
  for (auto &device : ascDevices) {
    device.registers = {};
    device.registers[5] = 3;
    device.registers[10] = 1;
    device.previousSource = false;
    device.transmitDelay = 0;
    device.transmitted.clear();
  }
  controller = {};
  routing = {};
  routing[0x1bc / 4] = 0x7fffffffU;
  for (uint32_t source = 0; source < 31; ++source) routing[0x100 / 4 + source] = 1;
  previousLevels = {};
  softwarePending = hardwarePending = 0;
  initialized = true;
  setPeripheralPoller(pollSti5518Devices);
  setInterruptReturnObserver(acknowledgeInterrupt);
  return 0;
}

void shutdownSti5518Devices() {
  if (!initialized) return;
  for (const auto &device : ascDevices) unregisterMemoryDevice(device.base);
  unregisterMemoryDevice(controllerBase);
  unregisterMemoryDevice(routingBase);
  setPeripheralPoller(nullptr);
  setInterruptReturnObserver(nullptr);
  initialized = false;
}

std::vector<uint8_t> takeSti5518Asc1Transmit() {
  return std::exchange(ascDevices[0].transmitted, {});
}

std::vector<uint8_t> takeSti5518Asc2Transmit() {
  return std::exchange(ascDevices[1].transmitted, {});
}