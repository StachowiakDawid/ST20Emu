#include "STi5512_Pwm.h"
#include "../../core/cpu/st20.h"
#include "../common/STi55xx_Asc.h"

#include <array>

/*
 * STi5512 Register Manual (7145562 B), chapter 22. Both counters are clocked from the
 * processor clock through their prescalers. The 8-bit PWM counter raises Int when it wraps;
 * the 32-bit capture/compare counter raises Cmp0-3 when it reaches PWM_nCompareVal.
 * Capture inputs are edges reported by the board; PWMOut and CompareOut pins are not modelled.
 */
namespace {
constexpr uint32_t pwmBase = 0x2000b000U;
constexpr uint32_t pwmEnd = 0x2000b067U;
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

std::array<uint32_t, (pwmEnd + 1 - pwmBase) / 4> pwm{};
uint64_t lastCycle = 0;
uint64_t pwmPhase = 0;
uint64_t capturePhase = 0;

void advance(uint64_t cycles) {
  uint32_t control = pwm[controlRegister];
  if (control & pwmEnable) {
    pwmPhase += cycles;
    uint64_t divider = (control & 0xfU) + 1;
    uint64_t total = pwm[countRegister] + pwmPhase / divider;
    pwmPhase %= divider;
    if (total > 0xff) pwm[statusRegister] |= overflowInterrupt;
    pwm[countRegister] = static_cast<uint32_t>(total & 0xffU);
  }
  if (control & captureEnable) {
    capturePhase += cycles;
    uint64_t divider = ((control >> 4) & 0x1fU) + 1;
    uint64_t ticks = capturePhase / divider;
    capturePhase %= divider;
    uint32_t start = pwm[captureCountRegister];
    for (uint32_t compare = 0; compare < 4 && ticks; ++compare) {
      if (static_cast<uint64_t>(pwm[compareRegister + compare] - start - 1U) < ticks)
        pwm[statusRegister] |= 0x20U << compare;
    }
    pwm[captureCountRegister] = start + static_cast<uint32_t>(ticks);
  }
}

int readPwm(void *, uint32_t address, int width, uint32_t *value) {
  int result = checkWordAccess(address, width);
  if (result) return result;
  uint32_t index = (address - pwmBase) / 4;
  if (index == ackRegister) return DEVICE_ACCESS_UNSUPPORTED;
  *value = pwm[index];
  return 0;
}

int writePwm(void *, uint32_t address, int width, uint32_t value) {
  int result = checkWordAccess(address, width);
  if (result) return result;
  uint32_t index = (address - pwmBase) / 4;
  if (index == statusRegister) return DEVICE_ACCESS_UNSUPPORTED;
  if (index == ackRegister) pwm[statusRegister] &= ~(value & 0x1ffU);
  else if (index == enableRegister) pwm[index] = value & 0x1ffU;
  else if (index == controlRegister) pwm[index] = value & 0x7ffU;
  else if (index == countRegister) pwm[index] = value & 0xffU;
  else pwm[index] = value;
  return 0;
}
}

MemoryDevice sti5512PwmDevice() {
  return {pwmBase, pwmEnd, nullptr, readPwm, writePwm};
}

void resetSti5512Pwm() {
  pwm = {};
  lastCycle = elapsedCpuCycles();
  pwmPhase = capturePhase = 0;
}

bool tickSti5512Pwm() {
  uint64_t now = elapsedCpuCycles();
  advance(now - lastCycle);
  lastCycle = now;
  return (pwm[statusRegister] & pwm[enableRegister]) != 0;
}

void sti5512PwmCaptureEdge(int input, bool rising) {
  if (input < 0 || input > 3) return;
  tickSti5512Pwm();
  uint32_t edges = pwm[captureEdgeRegister + static_cast<uint32_t>(input)] & 3U;
  if (!(edges & (rising ? 1U : 2U)) || !(pwm[controlRegister] & captureEnable)) return;
  pwm[captureValueRegister + static_cast<uint32_t>(input)] = pwm[captureCountRegister];
  pwm[statusRegister] |= 2U << input;
}
