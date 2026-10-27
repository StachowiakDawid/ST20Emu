#include "STi55xx_Inc.h"
#include "STi55xx_Asc.h"
#include "../../core/cpu/st20.h"

#include <bit>

namespace {
constexpr uint32_t levelBits = 0xffU;
constexpr uint32_t globalEnable = 0x10000U;
}

int readInc(void *context, uint32_t address, int width, uint32_t *value) {
  int result = checkWordAccess(address, width);
  if (result) return result;
  auto &controller = *static_cast<InterruptController *>(context);
  *value = controller.registers[(address - controller.base) / 4];
  return 0;
}

int writeInc(void *context, uint32_t address, int width, uint32_t value) {
  int result = checkWordAccess(address, width);
  if (result) return result;
  auto &controller = *static_cast<InterruptController *>(context);
  auto &inc = controller.registers;
  uint32_t offset = address - controller.base;
  uint32_t bits = value & levelBits;
  uint32_t maskBits = value & (globalEnable | levelBits);
  bool all = controller.clearAll && (value & globalEnable);
  if (offset == 0xc4) inc[0xc0 / 4] |= maskBits;
  else if (offset == 0xc8) inc[0xc0 / 4] = all ? 0 : inc[0xc0 / 4] & ~maskBits;
  else if (offset == 0xc0) inc[0xc0 / 4] = maskBits;
  else if (offset == 0x84) controller.softwarePending |= bits;
  else if (offset == 0x88) {
    uint32_t cleared = all ? levelBits : bits;
    controller.softwarePending &= ~cleared;
    controller.hardwarePending &= ~cleared;
  }
  else if (offset == 0x80) { controller.softwarePending = bits; controller.hardwarePending &= bits; }
  else if (offset == 0x104) inc[0x100 / 4] |= bits;
  else if (offset == 0x108) inc[0x100 / 4] &= all ? 0 : ~bits;
  else if (offset == 0x100) inc[0x100 / 4] = bits;
  else if (offset >= 0x40 && offset < 0x60) inc[offset / 4] = value & 7U;
  else inc[offset / 4] = value;
  inc[0x80 / 4] = controller.softwarePending | controller.hardwarePending;
  return 0;
}

void resetInc(InterruptController &controller) {
  controller.registers = {};
  controller.previousLevels = {};
  controller.softwarePending = controller.hardwarePending = 0;
}

void acknowledgeInc(InterruptController &controller) {
  uint32_t &executing = controller.registers[0x100 / 4];
  if (executing) {
    int level = std::bit_width(executing) - 1;
    if (level > activeExternalInterruptLevel()) executing &= ~(1U << level);
  }
}

int pollInc(InterruptController &controller, uint32_t levelLines) {
  auto &inc = controller.registers;
  for (uint32_t level = 0; level < 8; ++level) {
    bool line = (levelLines & (1U << level)) != 0;
    uint32_t mode = inc[0x40 / 4 + level] & 7U;
    uint32_t bit = 1U << level;
    if (mode == 1 || mode == 2) controller.hardwarePending &= ~bit;
    if (triggered(mode, line, controller.previousLevels[level])) controller.hardwarePending |= bit;
    controller.previousLevels[level] = line;
  }
  inc[0x80 / 4] = controller.hardwarePending | controller.softwarePending;
  uint32_t mask = inc[0xc0 / 4];
  uint32_t enabled = mask & globalEnable ? mask & levelBits : 0;
  for (int level = 7; level >= 0; --level) {
    uint32_t bit = 1U << level;
    if (!(inc[0x80 / 4] & enabled & bit) || (inc[0x100 / 4] & bit)) continue;
    if (inc[static_cast<std::size_t>(level)] == 0) continue;
    bool accepted = false;
    int result = serviceNativeInterrupt(static_cast<uint32_t>(level), inc[static_cast<std::size_t>(level)], &accepted);
    if (result) return result;
    if (accepted) {
      controller.softwarePending &= ~bit;
      controller.hardwarePending &= ~bit;
      inc[0x80 / 4] &= ~bit;
      inc[0x100 / 4] |= bit;
      break;
    }
  }
  return 0;
}
