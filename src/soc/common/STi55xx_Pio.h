#ifndef STI55XX_PIO_H
#define STI55XX_PIO_H

#include <array>
#include <cstdint>

/* called after a port's output register changes */
using PioOutputObserver = void (*)(int port, uint8_t out);
/* returns the pin levels a port reads; pins without an external signal read back out */
using PioInputSource = uint8_t (*)(int port, uint8_t out);

/* PIO port shared by the STi5512 and STi5518, whose PIO register blocks are identical (the
 * STi5518 adds PIO5). Out, C0, C1, C2, Comp and Mask are at 0x00-0x60 in steps of 0x10, each
 * with Set and Clear aliases at +4 and +8; In is at 0x10. Compare interrupts and the pin
 * configuration modes are not modelled. */
struct PioPort {
  uint32_t base;
  int index;
  std::array<uint8_t, 7> registers{}; // Out, In (unused), C0, C1, C2, Comp, Mask
  PioOutputObserver observer = nullptr;
  PioInputSource input = nullptr;
};

inline constexpr uint32_t pioRegisterEnd = 0x6b;

int readPio(void *context, uint32_t address, int width, uint32_t *value);
int writePio(void *context, uint32_t address, int width, uint32_t value);
/* register reset; the observer and input source are kept */
void resetPio(PioPort &port);

#endif
