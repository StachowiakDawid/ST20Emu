#include "STi55xx_Pwm.h"
#include "STi55xx_Asc.h"
#include "../../core/cpu/st20.h"
#include "../../core/memory/memory.h"

namespace {
constexpr uint32_t captureValueRegister = 0x10 / 4;
constexpr uint32_t compareRegister = 0x20 / 4;
constexpr uint32_t captureEdgeRegister = 0x30 / 4;
constexpr uint32_t controlRegister = 0x50 / 4;
constexpr uint32_t enableRegister = 0x54 / 4;
constexpr uint32_t statusRegister = 0x58 / 4;
constexpr uint32_t ackRegister = 0x5c / 4;
constexpr uint32_t countRegister = 0x60 / 4;
constexpr uint32_t captureCountRegister = 0x64 / 4;
constexpr uint32_t pwmEnable = 0x200;
constexpr uint32_t captureEnable = 0x400;
constexpr uint32_t overflowInterrupt = 0x001;

void advance(PwmDevice &device, uint64_t cycles) {
  auto &pwm = device.registers;
  uint32_t control = pwm[controlRegister];
  if (control & pwmEnable) {
    device.pwmPhase += cycles;
    uint64_t divider = (control & 0xfU) + 1;
    uint64_t total = pwm[countRegister] + device.pwmPhase / divider;
    device.pwmPhase %= divider;
    if (total > 0xff) pwm[statusRegister] |= overflowInterrupt;
    pwm[countRegister] = static_cast<uint32_t>(total & 0xffU);
  }
  if (control & captureEnable) {
    device.capturePhase += cycles;
    uint64_t divider = ((control >> 4) & 0x1fU) + 1;
    uint64_t ticks = device.capturePhase / divider;
    device.capturePhase %= divider;
    uint32_t start = pwm[captureCountRegister];
    for (uint32_t compare = 0; compare < 4 && ticks; ++compare) {
      if (static_cast<uint64_t>(pwm[compareRegister + compare] - start - 1U) < ticks)
        pwm[statusRegister] |= 0x20U << compare;
    }
    pwm[captureCountRegister] = start + static_cast<uint32_t>(ticks);
  }
}
}

int readPwm(void *context, uint32_t address, int width, uint32_t *value) {
  int result = checkWordAccess(address, width);
  if (result) return result;
  auto &device = *static_cast<PwmDevice *>(context);
  uint32_t index = (address - device.base) / 4;
  if (index == ackRegister) return DEVICE_ACCESS_UNSUPPORTED;
  *value = device.registers[index];
  return 0;
}

int writePwm(void *context, uint32_t address, int width, uint32_t value) {
  int result = checkWordAccess(address, width);
  if (result) return result;
  auto &device = *static_cast<PwmDevice *>(context);
  auto &pwm = device.registers;
  uint32_t index = (address - device.base) / 4;
  if (index == statusRegister) return DEVICE_ACCESS_UNSUPPORTED;
  if (index == ackRegister) pwm[statusRegister] &= ~(value & 0x1ffU);
  else if (index == enableRegister) pwm[index] = value & 0x1ffU;
  else if (index == controlRegister) pwm[index] = value & 0x7ffU;
  else if (index == countRegister) pwm[index] = value & 0xffU;
  else pwm[index] = value;
  return 0;
}

void resetPwm(PwmDevice &device) {
  device.registers = {};
  device.lastCycle = elapsedCpuCycles();
  device.pwmPhase = device.capturePhase = 0;
}

bool tickPwm(PwmDevice &device) {
  uint64_t now = elapsedCpuCycles();
  advance(device, now - device.lastCycle);
  device.lastCycle = now;
  return (device.registers[statusRegister] & device.registers[enableRegister]) != 0;
}

void pwmCaptureEdge(PwmDevice &device, int input, bool rising) {
  if (input < 0 || input > 3) return;
  tickPwm(device);
  auto &pwm = device.registers;
  uint32_t edges = pwm[captureEdgeRegister + static_cast<uint32_t>(input)] & 3U;
  if (!(edges & (rising ? 1U : 2U)) || !(pwm[controlRegister] & captureEnable)) return;
  pwm[captureValueRegister + static_cast<uint32_t>(input)] = pwm[captureCountRegister];
  pwm[statusRegister] |= 2U << input;
}
