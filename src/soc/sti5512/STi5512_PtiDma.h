#ifndef STI5512_PTI_DMA_H
#define STI5512_PTI_DMA_H

#include "../../core/memory/memory.h"

/* STi5512 PTI DMA registers (PTIBaseAddress + 0x1000 to 0x1063). Channels 1-3 feed the
 * video, audio and sub-picture decoders from circular buffers in memory. */
MemoryDevice sti5512PtiDmaDevice();
void resetSti5512PtiDma();
void tickSti5512PtiDma();
/* register at OFFSET from PTIBaseAddress + 0x1000 (base 0x00, top 0x10, write 0x20, read
 * 0x30, enable 0x50, CD FIFO page 0x60) */
uint32_t sti5512PtiDmaRegister(uint32_t offset);
/* the transport controller advancing a channel's write pointer */
void sti5512PtiDmaSetWrite(uint32_t channel, uint32_t address);

#endif
