#include "STi5512_Mpeg.h"

#ifdef ST20EMU_HAVE_LIBAV

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
}

#include <algorithm>
#include <cstring>

namespace {
struct Codec {
  AVCodecContext *context = nullptr;
  AVPacket *packet = nullptr;
  AVFrame *frame = nullptr;

  bool open(AVCodecID id) {
    if (context) return true;
    const AVCodec *codec = avcodec_find_decoder(id);
    if (!codec) return false;
    context = avcodec_alloc_context3(codec);
    /* the decoder writes each picture into its frame buffer as it decodes it: pictures are
     * wanted in decoding order, not reordered for display */
    if (context && id == AV_CODEC_ID_MPEG2VIDEO) context->flags |= AV_CODEC_FLAG_LOW_DELAY;
    packet = av_packet_alloc();
    frame = av_frame_alloc();
    if (!context || !packet || !frame || avcodec_open2(context, codec, nullptr) < 0) {
      close();
      return false;
    }
    return true;
  }

  void close() {
    avcodec_free_context(&context);
    av_packet_free(&packet);
    av_frame_free(&frame);
  }

  bool send(const uint8_t *data, std::size_t size, int64_t pts) {
    if (av_new_packet(packet, static_cast<int>(size)) < 0) return false;
    std::memcpy(packet->data, data, size);
    packet->pts = pts;
    int result = avcodec_send_packet(context, packet);
    av_packet_unref(packet);
    return result >= 0;
  }
};

Codec video;
Codec audio;

std::shared_ptr<const YuvFrame> copyFrame(const AVFrame *frame) {
  auto out = std::make_shared<YuvFrame>();
  out->width = frame->width;
  out->height = frame->height;
  bool chroma422 = frame->format == AV_PIX_FMT_YUV422P;
  bool chroma444 = frame->format == AV_PIX_FMT_YUV444P;
  out->chromaWidth = chroma444 ? frame->width : (frame->width + 1) / 2;
  out->chromaHeight = (chroma422 || chroma444) ? frame->height : (frame->height + 1) / 2;
  auto plane = [](std::vector<uint8_t> &to, const uint8_t *from, int stride, int width, int height) {
    to.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int row = 0; row < height; ++row)
      std::memcpy(to.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(width),
                  from + static_cast<std::ptrdiff_t>(row) * stride, static_cast<std::size_t>(width));
  };
  plane(out->y, frame->data[0], frame->linesize[0], out->width, out->height);
  plane(out->u, frame->data[1], frame->linesize[1], out->chromaWidth, out->chromaHeight);
  plane(out->v, frame->data[2], frame->linesize[2], out->chromaWidth, out->chromaHeight);
  return out;
}
} // namespace

bool mpegDecodingAvailable() {
  return true;
}

void mpegVideoReset() {
  video.close();
}

std::vector<std::pair<int64_t, std::shared_ptr<const YuvFrame>>> mpegVideoDecode(const uint8_t *data, std::size_t size,
                                                                                 int64_t tag) {
  std::vector<std::pair<int64_t, std::shared_ptr<const YuvFrame>>> ready;
  if (!video.open(AV_CODEC_ID_MPEG2VIDEO) || size == 0) return ready;
  video.send(data, size, tag);
  while (avcodec_receive_frame(video.context, video.frame) >= 0) {
    if (video.frame->format == AV_PIX_FMT_YUV420P || video.frame->format == AV_PIX_FMT_YUV422P ||
        video.frame->format == AV_PIX_FMT_YUV444P)
      ready.emplace_back(video.frame->pts, copyFrame(video.frame));
    av_frame_unref(video.frame);
  }
  return ready;
}

void mpegAudioReset() {
  audio.close();
}

PcmBlock mpegAudioDecode(const uint8_t *data, std::size_t size) {
  PcmBlock block;
  if (!audio.open(AV_CODEC_ID_MP2) || size == 0) return block;
  audio.send(data, size, AV_NOPTS_VALUE);
  while (avcodec_receive_frame(audio.context, audio.frame) >= 0) {
    const AVFrame *frame = audio.frame;
    int channels = frame->ch_layout.nb_channels;
    block.sampleRate = frame->sample_rate;
    block.channels = channels;
    auto format = static_cast<AVSampleFormat>(frame->format);
    bool planar = av_sample_fmt_is_planar(format);
    for (int i = 0; i < frame->nb_samples; ++i) {
      for (int c = 0; c < channels; ++c) {
        int index = planar ? i : i * channels + c;
        const uint8_t *source = planar ? frame->extended_data[c] : frame->extended_data[0];
        double sample = 0;
        switch (av_get_packed_sample_fmt(format)) {
          case AV_SAMPLE_FMT_S16: sample = reinterpret_cast<const int16_t *>(source)[index] / 32768.0; break;
          case AV_SAMPLE_FMT_S32: sample = reinterpret_cast<const int32_t *>(source)[index] / 2147483648.0; break;
          case AV_SAMPLE_FMT_FLT: sample = reinterpret_cast<const float *>(source)[index]; break;
          case AV_SAMPLE_FMT_DBL: sample = reinterpret_cast<const double *>(source)[index]; break;
          default: break;
        }
        block.samples.push_back(static_cast<int16_t>(std::clamp(sample * 32768.0, -32768.0, 32767.0)));
      }
    }
    av_frame_unref(audio.frame);
  }
  return block;
}

#else

bool mpegDecodingAvailable() {
  return false;
}

void mpegVideoReset() {}

std::vector<std::pair<int64_t, std::shared_ptr<const YuvFrame>>> mpegVideoDecode(const uint8_t *, std::size_t, int64_t) {
  return {};
}

void mpegAudioReset() {}

PcmBlock mpegAudioDecode(const uint8_t *, std::size_t) {
  return {};
}

#endif
