#ifndef STI5512_REGISTER_BLOCKS_H
#define STI5512_REGISTER_BLOCKS_H

#include "../../core/memory/memory.h"

#include <cstdint>
#include <vector>

/* STi5512 register blocks below 0x8000 (video/configuration, audio, sub-picture, DENC,
 * EMI, cache and MPEG control) modelled as byte register files with reset values. */
std::vector<MemoryDevice> sti5512RegisterBlockDevices();
void resetSti5512RegisterBlocks(uint8_t videoRevision);
/* stored value of a video/configuration register (VideoBaseAddress 0) */
uint8_t sti5512VideoRegister(uint32_t offset);
/* OSD_TOP (field 0) or OSD_BOT (field 1), in units of 256 bytes of SDRAM */
uint32_t sti5512OsdPointer(int field);
/* sets VID_ITS events: bits 23:16 = 0x3d, 15:8 = 0x62, 7:0 = 0x63 */
void raiseSti5512VideoInterrupt(uint32_t events);
bool sti5512VideoInterruptLine();

#endif
