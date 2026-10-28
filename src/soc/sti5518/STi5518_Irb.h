#ifndef STI5518_IRB_H
#define STI5518_IRB_H

#include "../../core/memory/memory.h"

#include <cstdint>

/* IR transmitter/receiver (IRBBaseAddress 0x2000a200), receive side of the RC-IR channel. */
MemoryDevice sti5518IrbDevice();
void resetSti5518Irb();
/* a received pulse: its duration and the time to the start of the next pulse, in microseconds;
 * LAST marks the final pulse of a frame */
void sti5518IrbReceive(uint32_t onMicroseconds, uint32_t periodMicroseconds, bool last);
/* interrupt source 18 */
bool sti5518IrbInterruptLine();

#endif
