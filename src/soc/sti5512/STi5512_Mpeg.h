#ifndef STI5512_MPEG_H
#define STI5512_MPEG_H

#include "../common/STi55xx_Display.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

/*
 * Picture and sound reconstruction for the STi5512 decoder models through libavcodec (FFmpeg),
 * when the build found it (meson option: the libavcodec dependency is auto-detected). Without
 * it the decoders keep their control behaviour and produce no pixels or samples.
 */
using YuvFrame = VideoPicture;

bool mpegDecodingAvailable();

/* MPEG-1/2 video: one coded picture (with any sequence/GOP headers before it) per call, in
 * decoding order; TAG identifies the picture. Returns the pictures that became ready, in
 * decoding order (low-delay output, as the hardware fills its frame buffers), with the tags
 * they were sent with. */
void mpegVideoReset();
std::vector<std::pair<int64_t, std::shared_ptr<const YuvFrame>>> mpegVideoDecode(const uint8_t *data, std::size_t size,
                                                                                 int64_t tag);

/* MPEG-1 layer I/II audio: one frame per call; interleaved 16-bit samples */
struct PcmBlock {
  int sampleRate = 0;
  int channels = 0;
  std::vector<int16_t> samples;
};
void mpegAudioReset();
PcmBlock mpegAudioDecode(const uint8_t *data, std::size_t size);

#endif
