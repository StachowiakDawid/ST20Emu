#ifndef STI5512_PES_H
#define STI5512_PES_H

#include <array>
#include <cstddef>
#include <cstdint>

/*
 * PES parser in front of the STi5512 video and audio decoders (Register Manual 7145562 B,
 * chapter 18: PES_CF1/PES_CF2, automatic configuration). The transport controller hands the
 * decoders the payload of their transport packets, i.e. PES packets; the parser strips the
 * PES headers (MPEG-2 and MPEG-1 syntax) and passes the elementary stream on. Until the first
 * PES start code (00 00 01 followed by a stream id 0xbc-0xff) it passes bytes through
 * unchanged, so elementary streams written straight into the decoders (still pictures,
 * CFG_CDR, AUD_CDI) work as well. Packets with a length are tracked by length; packets of
 * unbounded length (video) end at the next PES start code.
 */
class PesParser {
public:
  void reset() { *this = PesParser{}; }

  /* feeds one byte; OUT receives up to 4 elementary-stream bytes, the count is returned */
  std::size_t feed(uint8_t byte, uint8_t *out) {
    switch (state_) {
      case State::Payload:
        if (remaining_ > 0) {
          out[0] = byte;
          if (--remaining_ == 0) state_ = State::Expect;
          return 1;
        }
        return scan(byte, out);
      case State::Expect:
        return expect(byte, out);
      case State::Header:
        return header(byte);
      case State::Skip:
        if (--remaining_ == 0) state_ = State::Expect;
        return 0;
    }
    return 0;
  }

  bool inPesMode() const { return pesMode_; }

  /* the PTS of the PES packet whose payload starts with the next byte returned, once */
  bool takePts(uint64_t *pts) {
    if (!ptsReady_) return false;
    ptsReady_ = false;
    *pts = pts_;
    return true;
  }

private:
  enum class State { Payload, Expect, Header, Skip };

  static bool isPesId(uint8_t id) { return id >= 0xbc; }

  /* unbounded payload or pass-through: holds back up to three bytes that may begin a PES
   * start code; 00 00 01 xx with xx < 0xbc is elementary-stream data and passes */
  std::size_t scan(uint8_t byte, uint8_t *out) {
    static constexpr uint8_t prefix[3] = {0, 0, 1};
    window_[windowLength_++] = byte;
    std::size_t count = 0;
    for (;;) {
      std::size_t check = windowLength_ < 3 ? windowLength_ : 3;
      bool matches = true;
      for (std::size_t i = 0; i < check; ++i) matches = matches && window_[i] == prefix[i];
      if (matches) {
        if (windowLength_ < 4) return count;
        if (isPesId(window_[3])) {
          windowLength_ = 0;
          startHeader(window_[3]);
          return count;
        }
        for (std::size_t i = 0; i < 4; ++i) out[count++] = window_[i];
        windowLength_ = 0;
        return count;
      }
      out[count++] = window_[0];
      for (std::size_t i = 1; i < windowLength_; ++i) window_[i - 1] = window_[i];
      if (--windowLength_ == 0) return count;
    }
  }

  std::size_t expect(uint8_t byte, uint8_t *out) {
    window_[windowLength_++] = byte;
    static constexpr uint8_t prefix[3] = {0, 0, 1};
    if (windowLength_ <= 3 && byte == prefix[windowLength_ - 1]) return 0;
    if (windowLength_ == 4 && isPesId(byte)) {
      windowLength_ = 0;
      startHeader(byte);
      return 0;
    }
    // not a PES packet: fall back to scanning the bytes as payload
    state_ = State::Payload;
    remaining_ = 0;
    std::array<uint8_t, 4> held = window_;
    std::size_t length = windowLength_;
    windowLength_ = 0;
    std::size_t count = 0;
    for (std::size_t i = 0; i < length; ++i) count += scan(held[i], out + count);
    return count;
  }

  void startHeader(uint8_t id) {
    pesMode_ = true;
    streamId_ = id;
    headerBytes_ = 0;
    state_ = State::Header;
  }

  /* bytes after the stream id: length (2), then the header proper */
  std::size_t header(uint8_t byte) {
    ++headerBytes_;
    if (headerBytes_ == 1) {
      length_ = static_cast<uint32_t>(byte) << 8;
      return 0;
    }
    if (headerBytes_ == 2) {
      length_ |= byte;
      // streams without the optional header: padding, private_stream_2, ECM/EMM, DSM-CC...
      if (streamId_ == 0xbc || streamId_ == 0xbe || streamId_ == 0xbf || streamId_ == 0xf0 ||
          streamId_ == 0xf1 || streamId_ == 0xf2 || streamId_ == 0xf8 || streamId_ == 0xff) {
        remaining_ = length_;
        state_ = remaining_ ? State::Skip : State::Expect;
      }
      used_ = 0;
      mpeg1Stage_ = 0;
      return 0;
    }
    ++used_;
    if (headerBytes_ == 3) {
      if ((byte & 0xc0) == 0x80) {
        mpeg2_ = true;
        return 0;
      }
      mpeg2_ = false;
      return mpeg1(byte);
    }
    if (mpeg2_) {
      if (headerBytes_ == 4) hasPts_ = (byte & 0x80) != 0;
      if (headerBytes_ >= 6 && headerBytes_ <= 10) ptsBytes_[headerBytes_ - 6] = byte;
      if (headerBytes_ == 5) {
        extra_ = byte;
        if (extra_ == 0) beginPayload();
      } else if (headerBytes_ > 5 && --extra_ == 0) {
        beginPayload();
      }
      return 0;
    }
    return mpeg1(byte);
  }

  /* MPEG-1 packet header: stuffing 0xff, optional STD buffer (01xx, 2 bytes), then 0x0f or
   * a PTS (0010, 5 bytes) or PTS+DTS (0011, 10 bytes) */
  std::size_t mpeg1(uint8_t byte) {
    if (extra_ > 0 && mpeg1Stage_ != 0) {
      if (--extra_ == 0) {
        if (mpeg1Stage_ == 1) mpeg1Stage_ = 0; // STD buffer done, timestamps follow
        else beginPayload();
      }
      return 0;
    }
    if (byte == 0xff) return 0;
    if ((byte & 0xc0) == 0x40) {
      mpeg1Stage_ = 1;
      extra_ = 1;
      return 0;
    }
    if ((byte & 0xf0) == 0x20) {
      mpeg1Stage_ = 2;
      extra_ = 4;
      return 0;
    }
    if ((byte & 0xf0) == 0x30) {
      mpeg1Stage_ = 2;
      extra_ = 9;
      return 0;
    }
    beginPayload(); // 0x0f: no timestamps
    return 0;
  }

  void beginPayload() {
    if (mpeg2_ && hasPts_) {
      pts_ = static_cast<uint64_t>((ptsBytes_[0] >> 1) & 7) << 30 | static_cast<uint64_t>(ptsBytes_[1]) << 22 |
             static_cast<uint64_t>(ptsBytes_[2] >> 1) << 15 | static_cast<uint64_t>(ptsBytes_[3]) << 7 | (ptsBytes_[4] >> 1);
      ptsReady_ = true;
    }
    hasPts_ = false;
    state_ = State::Payload;
    remaining_ = length_ ? (length_ > used_ ? length_ - used_ : 0) : 0;
    if (length_ && remaining_ == 0) state_ = State::Expect;
  }

  State state_ = State::Payload;
  bool pesMode_ = false;
  std::array<uint8_t, 4> window_{};
  std::size_t windowLength_ = 0;
  uint8_t streamId_ = 0;
  uint32_t headerBytes_ = 0;
  uint32_t length_ = 0;
  uint32_t used_ = 0; // header bytes counted in the PES packet length
  uint32_t remaining_ = 0;
  uint32_t extra_ = 0;
  int mpeg1Stage_ = 0;
  bool hasPts_ = false;
  bool ptsReady_ = false;
  std::array<uint8_t, 5> ptsBytes_{};
  uint64_t pts_ = 0;
  bool mpeg2_ = true;
};

#endif
