#ifndef STI5512_PTI_H
#define STI5512_PTI_H

#include "../../core/memory/memory.h"

#include <cstdint>

/*
 * STi5512 programmable transport interface: the interrupt, TC mode and STC registers
 * (PTIBaseAddress 0x20030000 + 0x00-0x57) and a high-level transport controller that
 * interprets the tables the ST PTI drivers keep in the TC data memory (0x20038000) instead of
 * running the TC microcode. See STi5512_Pti.cpp for the table layout.
 */
MemoryDevice sti5512PtiRegisterDevice();
void resetSti5512Pti();
/* the PTI interrupt (number 14): any IntStatus bit with its IntEnable bit set */
bool sti5512PtiInterruptLine();
/* one 188-byte transport packet from the input interface (the front end) */
void sti5512PtiInput(const uint8_t *packet);
/* packets accepted by the input interface and packets that matched a slot, for tests */
uint64_t sti5512PtiInputPackets();
uint64_t sti5512PtiMatchedPackets();
/* units (sections, PES packets, payloads) dropped because their ring was full */
uint64_t sti5512PtiDroppedUnits();

#endif
