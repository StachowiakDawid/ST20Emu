#ifndef STI5512_VIDEO_DECODER_H
#define STI5512_VIDEO_DECODER_H

#include <cstdint>

/* STi5512 MPEG video decoder control: bit buffer, start code detector, header FIFO and the
 * decoding pipeline's task sequencing. Pictures are parsed for their extent only; no
 * picture is reconstructed. */
void resetSti5512VideoDecoder();
/* compressed data from the PTI DMA or CFG_CDR; returns false when the bit buffer is full */
bool sti5512VideoInput(uint8_t byte);
/* register access hooks for the video block: return true if the decoder supplied the value */
bool sti5512VideoDecoderRead(uint32_t offset, uint8_t *value);
void sti5512VideoDecoderWrite(uint32_t offset, uint8_t value);
/* called on every VSYNC before the VSYNC interrupt events */
void sti5512VideoDecoderVsync();
/* advances a running task and updates the status bits and their interrupt events */
void tickSti5512VideoDecoder();

#endif
