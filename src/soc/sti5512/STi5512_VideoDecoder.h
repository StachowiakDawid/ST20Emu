#ifndef STI5512_VIDEO_DECODER_H
#define STI5512_VIDEO_DECODER_H

#include "../common/STi55xx_Display.h"

#include <cstdint>
#include <memory>
#include <string>

/* STi5512 MPEG video decoder: PES parser, bit buffer, start code detector, header FIFO, the
 * decoding pipeline's task sequencing and (with libavcodec) picture reconstruction. */
void resetSti5512VideoDecoder();
/* compressed data from the PTI DMA or CFG_CDR; returns false when the bit buffer is full */
bool sti5512VideoInput(uint8_t byte);
/* register access hooks for the video block: return true if the decoder supplied the value */
bool sti5512VideoDecoderRead(uint32_t offset, uint8_t *value);
void sti5512VideoDecoderWrite(uint32_t offset, uint8_t value);
/* called on every VSYNC before the VSYNC interrupt events */
void sti5512VideoDecoderVsync();
/* the picture the display shows (null: black, video display disabled or nothing decoded) */
std::shared_ptr<const VideoPicture> sti5512DisplayedPicture();
/* one-line state for the `video` command */
std::string sti5512VideoStatus();
/* advances a running task and updates the status bits and their interrupt events */
void tickSti5512VideoDecoder();

#endif
