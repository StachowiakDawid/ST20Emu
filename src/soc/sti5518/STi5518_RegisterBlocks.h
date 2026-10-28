#ifndef STI5518_REGISTER_BLOCKS_H
#define STI5518_REGISTER_BLOCKS_H

#include "../../core/memory/memory.h"

#include <cstdint>
#include <vector>

/* STi5518 MPEG register blocks at 0x000-0xfff (video and clock generator, audio,
 * sub-picture, DENC, compressed-data FIFOs and MPEG_CONTROL) modelled as byte register files
 * with reset values. */
std::vector<MemoryDevice> sti5518RegisterBlockDevices();
void resetSti5518RegisterBlocks();
/* stored value of a video register (VideoBaseAddress 0) */
uint8_t sti5518VideoRegister(uint32_t offset);
/* OSD_OTP (field 0) or OSD_OBP (field 1), in units of 256 bytes of SDRAM */
uint32_t sti5518OsdPointer(int field);
/* sets VID_ITS events: bits 23:16 = 0x3d, 15:8 = 0x62, 7:0 = 0x63 */
void raiseSti5518VideoInterrupt(uint32_t events);
/* interrupt sources 28 (video) and 27 (audio) */
bool sti5518VideoInterruptLine();
bool sti5518AudioInterruptLine();

#endif
