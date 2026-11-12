#ifndef STI5512_AUDIO_DECODER_H
#define STI5512_AUDIO_DECODER_H

#include <cstdint>
#include <string>

/*
 * STi5512 MPEG audio decoder (Register Manual 7145562 B chapter 4, AudioBaseAddress 0x800):
 * compressed data from PTI DMA channel 2 or AUD_CDI passes the PES parser into the audio bit
 * buffer; while AUD_PLY is set and AUD_RES is clear, MPEG-1/2 layer II frames are decoded
 * (libavcodec) at their real-time rate. `audio FILE.wav` captures the PCM output.
 */
void resetSti5512AudioDecoder();
/* compressed data in; returns false while the bit buffer is full */
bool sti5512AudioInput(uint8_t byte);
/* register hooks for offsets inside the audio block; read returns true if it supplied VALUE */
bool sti5512AudioDecoderRead(uint32_t offset, uint8_t *value);
void sti5512AudioDecoderWrite(uint32_t offset, uint8_t value);
/* the audio decoder interrupt (number 22): AUD_ITR & AUD_ITM */
bool sti5512AudioInterruptLine();
/* decodes the frames that are due */
void tickSti5512AudioDecoder();
/* audio bit buffer level in units of 256 bytes (VID_ABL) */
uint32_t sti5512AudioLevel();

/* starts writing the decoded PCM to a 16-bit WAV file, silent while AUD_MUT is set unless
 * IGNORE_MUTE; returns an error message or "" */
std::string sti5512AudioCaptureStart(const std::string &path, bool ignoreMute = false);
/* finishes the WAV file; returns a summary */
std::string sti5512AudioCaptureStop();
std::string sti5512AudioStatus();
/* for tests: frames decoded since reset */
uint64_t sti5512AudioFramesDecoded();

#endif
