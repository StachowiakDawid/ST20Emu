#ifndef STI55XX_PWM_H
#define STI55XX_PWM_H

#include <array>
#include <cstdint>

/* PWM and counter module shared by the STi5512 and STi5518, whose register blocks are
 * identical (Register Manuals 7145562 B chapter 22, 7170180A "PWM and counter module").
 * Both counters are clocked from the processor clock through their prescalers. The 8-bit PWM
 * counter raises Int when it wraps; the 32-bit capture/compare counter raises Cmp0-3 when it
 * reaches PWM_nCompareVal. Capture inputs are edges reported by the board; the PWMOut and
 * CompareOut pins are not modelled. */
struct PwmDevice {
  uint32_t base;
  std::array<uint32_t, 0x68 / 4> registers{};
  uint64_t lastCycle = 0;
  uint64_t pwmPhase = 0;
  uint64_t capturePhase = 0;
};

inline constexpr uint32_t pwmRegisterEnd = 0x67;

int readPwm(void *context, uint32_t address, int width, uint32_t *value);
int writePwm(void *context, uint32_t address, int width, uint32_t value);
void resetPwm(PwmDevice &device);
/* advances the counters to the current CPU cycle; returns the interrupt line */
bool tickPwm(PwmDevice &device);
/* an edge on pin CaptureIn0-3: latches PWM_CaptureCount if PWM_nCaptureEdge selects it */
void pwmCaptureEdge(PwmDevice &device, int input, bool rising);

#endif
