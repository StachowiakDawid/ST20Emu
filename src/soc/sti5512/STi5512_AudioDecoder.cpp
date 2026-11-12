#include "STi5512_AudioDecoder.h"
#include "STi5512_Mpeg.h"
#include "STi5512_Pes.h"
#include "STi5512_RegisterBlocks.h"
#include "../../core/cpu/omr.h"
#include "../../core/cpu/st20.h"

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <format>
#include <vector>

/*
 * Modelled registers (offsets in the audio block): AUD_CDI 0x18 (compressed data input),
 * AUD_SYS 0x26 (11 = locked while frames decode, 00 otherwise), AUD_PLY 0x2e bit 0 (decode),
 * AUD_MUT 0x30 bit 0 (output silence), AUD_RES 0x40 (any write resets: empties the bit buffer),
 * AUD_RST 0x42 bit 0 (restart: empties the bit buffer), AUD_SKP 0x32 bit 0 (the next frame is
 * dropped), AUD_SFR 0x44 (sampling
 * frequency code of the last frame), AUD_HDR 0x5e-0x61 (last frame header, 0x61 = first
 * byte), AUD_PTS 0x62-0x66 (the PTS of the PES packet the frame starts in), AUD_ITR 0x1a/0x1b
 * with AUD_ITM 0x1c/0x1d (interrupt 22): bit 0 sync status change (cleared by reading AUD_SYS),
 * 1 header (cleared by reading 0x61), 2 PTS (cleared by reading 0x66), 9 sampling frequency
 * change, 14 new frame at the output (bits 14:8 cleared by reading 0x1b); bits are only set
 * while enabled in AUD_ITM. The bit buffer holds (VID_ABS - VID_ABG + 1) * 256 bytes (video registers 0x1c-0x21;
 * 16 KB when unset); VID_ABL reports its level. CRC, PCM underflow and FIFO-threshold interrupts, ancillary data,
 * attenuation and the PCM output format are not modelled; the WAV capture is 16-bit at the
 * stream's rate.
 */
namespace {
constexpr uint32_t cdiOffset = 0x18;
constexpr uint32_t sysOffset = 0x26;
constexpr uint32_t playOffset = 0x2e;
constexpr uint32_t muteOffset = 0x30;
constexpr uint32_t resetOffset = 0x40;
constexpr uint32_t restartOffset = 0x42;
constexpr uint32_t sfrOffset = 0x44;
constexpr uint32_t headerOffset = 0x5e;

std::deque<uint8_t> buffer;
PesParser pes;
bool locked = false;
uint8_t header[4]{};
uint8_t sampleRateCode = 0;
uint64_t nextFrameCycle = 0;
bool running = false;
uint64_t framesDecoded = 0;
uint16_t interrupts = 0;        // AUD_ITR
uint64_t inputEs = 0;           // elementary-stream bytes put into the bit buffer
uint64_t takenEs = 0;           // elementary-stream bytes taken out of it
std::deque<std::pair<uint64_t, uint64_t>> timestamps; // (ES position, PTS) of PES packets
uint64_t framePts = 0;          // AUD_PTS
int lastRate = -1;
/* ST20_AUDIO_TRACE=1: print frames with a PTS and play/stop changes on stderr */
const bool trace = std::getenv("ST20_AUDIO_TRACE") != nullptr;
bool skipNext = false; // AUD_SKP
constexpr uint32_t skipOffset = 0x32;
constexpr uint32_t itrOffset = 0x1a;
constexpr uint32_t itmOffset = 0x1c;
constexpr uint32_t ptsOffset = 0x62;
constexpr uint16_t itrSync = 0x0001;
constexpr uint16_t itrHeader = 0x0002;
constexpr uint16_t itrPts = 0x0004;
constexpr uint16_t itrRate = 0x0200;
constexpr uint16_t itrNewFrame = 0x4000;

uint16_t interruptMask() {
  return static_cast<uint16_t>(sti5512AudioRegister(itmOffset) | (sti5512AudioRegister(itmOffset + 1) & 0x7f) << 8);
}

/* AUD_ITR bits are only set while enabled in AUD_ITM */
void raise(uint16_t events) {
  interrupts = static_cast<uint16_t>(interrupts | (events & interruptMask()));
}

void setLocked(bool value) {
  if (value != locked) raise(itrSync);
  locked = value;
}

std::FILE *wav = nullptr;
std::string wavPath;
uint64_t wavFrames = 0; // sample frames written
int wavRate = 0;
int wavChannels = 0;
bool wavIgnoresMute = false;

uint8_t audioRegister(uint32_t offset) {
  return sti5512AudioRegister(offset);
}

std::size_t capacity() {
  uint32_t begin = static_cast<uint32_t>(sti5512VideoRegister(0x1c) & 0x3f) << 8 | sti5512VideoRegister(0x1d);
  uint32_t stop = static_cast<uint32_t>(sti5512VideoRegister(0x20) & 0x3f) << 8 | sti5512VideoRegister(0x21);
  return stop > begin ? (stop - begin + 1) * 256U : 16384U;
}

struct FrameInfo {
  std::size_t length = 0;
  int samples = 0;
  int rate = 0;
  int rateCode = 0;
};

/* MPEG-1/2 layer II frame header at buffer[i] */
bool parseHeader(std::size_t i, FrameInfo *info) {
  if (i + 4 > buffer.size() || buffer[i] != 0xff || (buffer[i + 1] & 0xe0) != 0xe0) return false;
  int version = (buffer[i + 1] >> 3) & 3; // 3 MPEG-1, 2 MPEG-2, 0 MPEG-2.5
  int layer = (buffer[i + 1] >> 1) & 3;   // 2 = layer II
  int bitrateIndex = buffer[i + 2] >> 4;
  int rateIndex = (buffer[i + 2] >> 2) & 3;
  int padding = (buffer[i + 2] >> 1) & 1;
  if (version == 1 || layer != 2 || bitrateIndex == 0 || bitrateIndex == 15 || rateIndex == 3) return false;
  static constexpr int mpeg1Rates[14] = {32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384};
  static constexpr int mpeg2Rates[14] = {8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
  static constexpr int sampleRates[3] = {44100, 48000, 32000};
  int bitrate = (version == 3 ? mpeg1Rates : mpeg2Rates)[bitrateIndex - 1] * 1000;
  int rate = sampleRates[rateIndex] >> (version == 3 ? 0 : version == 2 ? 1 : 2);
  info->length = static_cast<std::size_t>(144 * bitrate / rate + padding);
  info->samples = 1152;
  info->rate = rate;
  info->rateCode = rateIndex == 0 ? 0 : rateIndex == 1 ? 1 : 2;
  return true;
}

void writeLe(std::FILE *file, uint32_t value, int bytes) {
  for (int i = 0; i < bytes; ++i) std::fputc(static_cast<int>((value >> (8 * i)) & 0xff), file);
}

void writeWavHeader() {
  uint32_t dataBytes = static_cast<uint32_t>(wavFrames * static_cast<uint64_t>(wavChannels) * 2);
  std::fseek(wav, 0, SEEK_SET);
  std::fputs("RIFF", wav);
  writeLe(wav, 36 + dataBytes, 4);
  std::fputs("WAVEfmt ", wav);
  writeLe(wav, 16, 4);
  writeLe(wav, 1, 2);
  writeLe(wav, static_cast<uint32_t>(wavChannels), 2);
  writeLe(wav, static_cast<uint32_t>(wavRate), 4);
  writeLe(wav, static_cast<uint32_t>(wavRate * wavChannels * 2), 4);
  writeLe(wav, static_cast<uint32_t>(wavChannels * 2), 2);
  writeLe(wav, 16, 2);
  std::fputs("data", wav);
  writeLe(wav, dataBytes, 4);
  std::fseek(wav, 0, SEEK_END);
}

/* decodes the frame at the front of the bit buffer; false if none is complete */
bool decodeFrame() {
  FrameInfo info;
  std::size_t start = 0;
  while (start + 4 <= buffer.size() && !parseHeader(start, &info)) ++start;
  buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(start));
  takenEs += start;
  if (buffer.size() < 4 || buffer.size() < info.length) return false;
  std::vector<uint8_t> frame(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(info.length));
  buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(info.length));
  /* the PTS of the PES packet the frame starts in */
  bool hasPts = false;
  while (!timestamps.empty() && timestamps.front().first <= takenEs) {
    hasPts = true;
    framePts = timestamps.front().second;
    timestamps.pop_front();
  }
  takenEs += info.length;
  if (skipNext) { // AUD_SKP: the frame is dropped and the next one plays in its place
    skipNext = false;
    return decodeFrame();
  }
  for (int i = 0; i < 4; ++i) header[i] = frame[static_cast<std::size_t>(i)];
  sampleRateCode = static_cast<uint8_t>(info.rateCode);
  setLocked(true);
  raise(static_cast<uint16_t>(itrHeader | itrNewFrame | (hasPts ? itrPts : 0) | (info.rate != lastRate ? itrRate : 0)));
  if (trace && hasPts)
    std::fprintf(stderr, "AUDIO frame PTS %.3f s at %.3f s, bit buffer %zu\n", static_cast<double>(framePts) / 90000,
                 static_cast<double>(elapsedCpuCycles()) / CPU_CLOCK, buffer.size());
  lastRate = info.rate;
  ++framesDecoded;
  PcmBlock pcm = mpegAudioDecode(frame.data(), frame.size());
  if (wav && pcm.channels > 0) {
    if (wavRate == 0) {
      wavRate = pcm.sampleRate;
      wavChannels = pcm.channels;
    }
    if (pcm.channels == wavChannels) {
      bool mute = !wavIgnoresMute && (audioRegister(muteOffset) & 1) != 0;
      for (int16_t sample : pcm.samples) writeLe(wav, static_cast<uint16_t>(mute ? 0 : sample), 2);
      wavFrames += pcm.samples.size() / static_cast<std::size_t>(wavChannels);
    }
  }
  nextFrameCycle += static_cast<uint64_t>(info.samples) * CPU_CLOCK / static_cast<uint64_t>(info.rate);
  return true;
}

void emptyBuffer() {
  buffer.clear();
  pes.reset();
  locked = false;
  running = false;
  interrupts = 0;
  skipNext = false;
  inputEs = takenEs = 0;
  timestamps.clear();
  lastRate = -1;
  mpegAudioReset();
}
} // namespace

void resetSti5512AudioDecoder() {
  emptyBuffer();
  framesDecoded = 0;
  sampleRateCode = 0;
  for (uint8_t &byte : header) byte = 0;
}

bool sti5512AudioInput(uint8_t byte) {
  if (buffer.size() + 4 > capacity()) return false;
  uint8_t out[4];
  std::size_t count = pes.feed(byte, out);
  uint64_t pts = 0;
  if (pes.takePts(&pts)) {
    timestamps.emplace_back(inputEs + count, pts);
    if (timestamps.size() > 64) timestamps.pop_front();
  }
  buffer.insert(buffer.end(), out, out + count);
  inputEs += count;
  return true;
}

bool sti5512AudioDecoderRead(uint32_t offset, uint8_t *value) {
  switch (offset) {
    case itrOffset: // reading clears bit 5
      *value = static_cast<uint8_t>(interrupts);
      interrupts = static_cast<uint16_t>(interrupts & ~0x20);
      return true;
    case itrOffset + 1: // reading clears bits 14:8
      *value = static_cast<uint8_t>(interrupts >> 8 & 0x7f);
      interrupts &= 0xff;
      return true;
    case ptsOffset: *value = static_cast<uint8_t>(framePts); return true;
    case ptsOffset + 1: *value = static_cast<uint8_t>(framePts >> 8); return true;
    case ptsOffset + 2: *value = static_cast<uint8_t>(framePts >> 16); return true;
    case ptsOffset + 3: *value = static_cast<uint8_t>(framePts >> 24); return true;
    case ptsOffset + 4: // AUD_PTS[32]: reading clears AUD_ITR[2]
      *value = static_cast<uint8_t>(framePts >> 32 & 1);
      interrupts = static_cast<uint16_t>(interrupts & ~itrPts);
      return true;
    case sysOffset:
      *value = locked ? 3 : 0;
      interrupts = static_cast<uint16_t>(interrupts & ~itrSync);
      return true;
    case sfrOffset: *value = sampleRateCode; return true;
    case headerOffset: *value = header[3]; return true;
    case headerOffset + 1: *value = header[2]; return true;
    case headerOffset + 2: *value = header[1]; return true;
    case headerOffset + 3: // AUD_HDR[31:24]: reading clears AUD_ITR[1]
      *value = header[0];
      interrupts = static_cast<uint16_t>(interrupts & ~itrHeader);
      return true;
    default: return false;
  }
}

void sti5512AudioDecoderWrite(uint32_t offset, uint8_t value) {
  switch (offset) {
    case cdiOffset: sti5512AudioInput(value); break;
    case skipOffset:
      if (value & 1) skipNext = true;
      break;
    case resetOffset: // writing 0 or 1 resets the decoder (Register Manual: AUD_RES)
      emptyBuffer();
      break;
    case restartOffset:
      if (value & 1) emptyBuffer();
      break;
    default: break;
  }
}

void tickSti5512AudioDecoder() {
  bool playing = (audioRegister(playOffset) & 1) != 0;
  if (!playing) {
    if (trace && running) std::fprintf(stderr, "AUDIO stop at %.3f s\n", static_cast<double>(elapsedCpuCycles()) / CPU_CLOCK);
    running = false;
    return;
  }
  uint64_t now = elapsedCpuCycles();
  if (!running) {
    if (trace) std::fprintf(stderr, "AUDIO play at %.3f s\n", static_cast<double>(now) / CPU_CLOCK);
    running = true;
    nextFrameCycle = now;
  }
  for (int frames = 0; frames < 8 && now >= nextFrameCycle; ++frames) {
    if (!decodeFrame()) {
      nextFrameCycle = now; // underflow: restart the clock with the next frame
      break;
    }
  }
}

uint32_t sti5512AudioLevel() {
  return static_cast<uint32_t>(buffer.size() / 256);
}

std::string sti5512AudioCaptureStart(const std::string &path, bool ignoreMute) {
  if (wav) sti5512AudioCaptureStop();
  wavIgnoresMute = ignoreMute;
  wav = std::fopen(path.c_str(), "wb");
  if (!wav) return "cannot open " + path;
  wavPath = path;
  wavFrames = 0;
  wavRate = 0;
  wavChannels = 2;
  writeWavHeader();
  return mpegDecodingAvailable() ? std::string{} : "warning: built without libavcodec, the capture stays empty";
}

std::string sti5512AudioCaptureStop() {
  if (!wav) return "no audio capture running";
  if (wavRate == 0) wavRate = 48000;
  writeWavHeader();
  std::fclose(wav);
  wav = nullptr;
  return std::format("Wrote {} ({} samples at {} Hz, {} channels)", wavPath, wavFrames, wavRate, wavChannels);
}

std::string sti5512AudioStatus() {
  return std::format("audio decoder: {}{}, {} frames decoded, bit buffer {} bytes, capture {}",
                     (audioRegister(playOffset) & 1) ? "playing" : "stopped",
                     (audioRegister(muteOffset) & 1) ? " (muted)" : "", framesDecoded,
                     buffer.size(), wav ? wavPath : std::string("off"));
}

uint64_t sti5512AudioFramesDecoded() {
  return framesDecoded;
}

bool sti5512AudioInterruptLine() {
  return (interrupts & interruptMask()) != 0;
}
