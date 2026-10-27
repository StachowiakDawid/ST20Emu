#ifndef STI55XX_INC_H
#define STI55XX_INC_H

#include <array>
#include <cstdint>

/* interrupt trigger modes of the STi55xx interrupt controller (INC_TriggerMode) */
inline bool triggered(uint32_t mode, bool current, bool previous) {
  switch (mode & 7U) {
    case 1: return current;
    case 2: return !current;
    case 3: return current && !previous;
    case 4: return !current && previous;
    case 5: return current != previous;
    default: return false;
  }
}

/* Interrupt controller (INC) shared by the STi5512 and STi5518, whose INC register maps are
 * identical: HandlerWptr 0x00-0x1c, TriggerMode 0x40-0x5c, Pending 0x80, Mask 0xc0 (IntEn
 * bits 0-7 and GloEn bit 16) and Exec 0x100, each with set/clear registers at +4/+8.
 * Delivery requires GloEn. clearAll selects the STi5518 behaviour, where bit 16 of a clear
 * register clears every bit (Register Manual 7170180A); on the STi5512 it clears only GloEn. */
struct InterruptController {
  uint32_t base;
  bool clearAll;
  std::array<uint32_t, 0x10c / 4> registers{};
  std::array<bool, 8> previousLevels{};
  uint32_t softwarePending = 0;
  uint32_t hardwarePending = 0;
};

inline constexpr uint32_t incRegisterEnd = 0x10b;

int readInc(void *context, uint32_t address, int width, uint32_t *value);
int writeInc(void *context, uint32_t address, int width, uint32_t value);
void resetInc(InterruptController &controller);
/* clears the Exec bit of the interrupt the CPU has just returned from */
void acknowledgeInc(InterruptController &controller);
/* latches the level lines through the trigger modes and requests the highest pending level */
int pollInc(InterruptController &controller, uint32_t levelLines);

#endif
