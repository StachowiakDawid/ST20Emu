#ifndef STI5512_PTI_DMA_H
#define STI5512_PTI_DMA_H

#include "../../core/memory/memory.h"

/* STi5512 PTI DMA registers (PTIBaseAddress + 0x1000 to 0x1063). Channels 1-3 feed the
 * video, audio and sub-picture decoders from circular buffers in memory. */
MemoryDevice sti5512PtiDmaDevice();
void resetSti5512PtiDma();
void tickSti5512PtiDma();

#endif
