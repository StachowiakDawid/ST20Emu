#ifndef STI5512_REGISTER_BLOCKS_H
#define STI5512_REGISTER_BLOCKS_H

#include "../../core/memory/memory.h"

#include <cstdint>
#include <vector>

/* STi5512 register blocks below 0x8000 (video/configuration, audio, sub-picture, DENC,
 * EMI, cache and MPEG control) modelled as byte register files with reset values. */
std::vector<MemoryDevice> sti5512RegisterBlockDevices();
void resetSti5512RegisterBlocks(uint8_t videoRevision);

#endif
