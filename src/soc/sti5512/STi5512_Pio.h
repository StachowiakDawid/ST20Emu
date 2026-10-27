#ifndef STI5512_PIO_H
#define STI5512_PIO_H

#include "../../core/memory/memory.h"

#include <cstdint>
#include <vector>

/* STi5512 PIO ports 0-4 (PIOBaseAddress 0x2000c000 + port * 0x1000). */
inline constexpr int sti5512PioPorts = 5;

/* called after a port's output register changes */
using PioOutputObserver = void (*)(int port, uint8_t out);
/* returns the pin levels a port reads; pins without an external signal read back out */
using PioInputSource = uint8_t (*)(int port, uint8_t out);

std::vector<MemoryDevice> sti5512PioDevices();
void resetSti5512Pio();
void setSti5512PioObserver(PioOutputObserver observer);
void setSti5512PioInputSource(PioInputSource source);
uint8_t sti5512PioOutput(int port);

#endif
