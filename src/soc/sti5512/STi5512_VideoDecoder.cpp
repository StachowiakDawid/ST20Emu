#include "STi5512_VideoDecoder.h"
#include "STi5512_AudioDecoder.h"
#include "STi5512_Mpeg.h"
#include "STi5512_Pes.h"
#include "STi5512_RegisterBlocks.h"
#include "../../core/cpu/omr.h"
#include "../../core/cpu/st20.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <format>
#include <string>
#include <map>
#include <memory>
#include <vector>

/*
 * STi5512 datasheet (7145560 B) chapter 19 and Register Manual (7145562 B) VID registers.
 *
 * Compressed data enters the bit buffer; VID_VBL is its level in units of 2 Kbits (256 bytes)
 * and only the pipeline consumes it. The start code detector reads ahead through the header
 * FIFO: a search (VID_HDS.HDS or DSYNC) stops at the next start code other than a slice
 * (0x02-0xaf; 0x01 too unless VID_HDS.SOS) and sets SCH; the first search after a reset only
 * accepts a sequence header, and each search resumes after the previous hit. VID_HDF then
 * returns the start code's last byte (SCM = 1) and the header bytes that follow.
 *
 * The pipeline starts a task (DSYNC: PSD, start code search) at VSYNC, or at once with
 * VID_TIS.FIS, when VID_TIS.EXE is set and VID_CTL.EDC enables decoding. The first task after
 * a reset skips to the first picture after a sequence header; later tasks consume one picture
 * (up to the next picture, GOP or sequence start code), skipping VID_TIS.SKP pictures first;
 * SKP = 11 skips one picture and stops, and runs without EXE as well. The pipeline
 * then becomes idle (PII, DEI).
 *
 * Input passes the PES parser first (STi5512_Pes.h: PES_CF2 automatic mode), so the PTI's PES
 * packets and plain elementary streams both reach the bit buffer as elementary stream. When the
 * detector finds a picture start code, PES_TS (0x49-0x4d) takes the PTS of the PES packet the
 * picture starts in, with PES_TS.TSA (0x4d bit 1) set, or TSA clear when it has none.
 *
 * Reconstruction (with libavcodec, see STi5512_Mpeg.h): every picture a task consumes, skipped
 * ones included so that references stay intact, is decoded in decoding order; the picture the
 * task decodes is filed under the luma frame buffer VID_RFP (0x0e/0x0f) held at launch. The
 * frame buffers themselves are not written: the display takes, at each VSYNC unless
 * VID_DCF.FRZ (0x74 bit 0) freezes it, the picture filed under VID_DFP (0x0c/0x0d), or the
 * latest picture when none is (on-the-fly display); VID_DCF.EVD (0x75 bit 5) clear shows black.
 */
namespace {
// status/interrupt bits: 23:16 = VID_STA 0x3b / VID_ITS 0x3d, 15:8 = 0x64 / 0x62, 7:0 = 0x65 / 0x63
constexpr uint32_t statusPii = 0x0100;
constexpr uint32_t statusDei = 0x0200;
constexpr uint32_t statusHff = 0x1000;
constexpr uint32_t statusBmi = 0x2000;
constexpr uint32_t statusSch = 0x0001;
constexpr uint32_t statusHfe = 0x0004;
constexpr uint32_t statusBbf = 0x0008;
constexpr uint32_t statusBbe = 0x0010;
constexpr uint32_t statusPsd = 0x0080;

constexpr uint8_t controlEnable = 0x01;   // VID_CTL.EDC
constexpr uint8_t controlSoftReset = 0x02; // VID_CTL.SRS
constexpr uint8_t controlPipeReset = 0x04; // VID_CTL.PRS
constexpr uint8_t taskExecute = 0x01;     // VID_TIS.EXE
constexpr uint8_t taskForce = 0x04;       // VID_TIS.FIS
constexpr uint8_t searchHeader = 0x01;    // VID_HDS.HDS
constexpr uint8_t searchSlice = 0x04;     // VID_HDS.SOS
constexpr uint8_t startOnMsb = 0x08;      // VID_HDS.SCM
constexpr uint8_t sequenceCode = 0xb3;
constexpr uint8_t pictureCode = 0x00;
constexpr std::size_t headerFifoFull = 66;
constexpr uint64_t taskCycles = CPU_CLOCK / 1000; // 1 ms per decoding task
constexpr std::size_t notFound = SIZE_MAX;

enum class Pipeline { SeekSequence, Idle, Decoding };

std::deque<uint8_t> buffer; // bit buffer; index 0 is the pipeline read position
/* the header FIFO is filled by the start code detector, not the pipeline: it keeps its bytes
 * when a task consumes the bit buffer, so its read position is an absolute stream position
 * and recently consumed bytes are kept in `consumed` */
uint64_t headerPosition = 0;
std::deque<uint8_t> consumed;
constexpr std::size_t consumedKept = 16384;
std::size_t searchPosition = 0;  // where a running search resumes
bool searching = false;
bool foundStartCode = false;
bool sequenceOnly = true;
bool sliceStop = false;
Pipeline pipeline = Pipeline::SeekSequence;
uint8_t task = 0;
uint8_t skips = 0;
bool skipOnly = false; // the running task only skips a picture
uint64_t taskEnd = 0;
bool stalled = false; // the running task waits for more data
uint32_t status = 0;
PesParser pes;
uint64_t esBytes = 0; // elementary-stream bytes put into the bit buffer since the soft reset
std::deque<std::pair<uint64_t, uint64_t>> timestamps; // (ES position, PTS) of PES packets
bool timestampValid = false;                          // PES_TS.TSA
uint64_t timestamp = 0;
std::vector<uint8_t> pendingHeaders; // sequence/GOP headers for the next coded picture
int64_t taskTag = -1;                // VID_RFP of the running task
std::map<int64_t, std::shared_ptr<const VideoPicture>> frameBuffers;
std::shared_ptr<const VideoPicture> latestPicture;
std::shared_ptr<const VideoPicture> displayedPicture;
constexpr std::size_t maxFrameBuffers = 16;
/* ST20_VIDEO_TRACE=1: print task launches, completions, stalls and soft resets on stderr */
const bool trace = std::getenv("ST20_VIDEO_TRACE") != nullptr;
uint64_t tasksLaunched = 0;
uint64_t picturesSent = 0;
uint64_t picturesDecoded = 0;
uint64_t inputBytes = 0;

uint32_t pointerRegister(uint32_t offset) {
  return (static_cast<uint32_t>(sti5512VideoRegister(offset)) << 8 | sti5512VideoRegister(offset + 1)) & 0x3fff;
}

/* decodes buffer bytes [FROM, TO): any headers and one coded picture */
void decodePicture(std::size_t from, std::size_t to, int64_t tag) {
  std::vector<uint8_t> packet = std::move(pendingHeaders);
  pendingHeaders.clear();
  packet.insert(packet.end(), buffer.begin() + static_cast<std::ptrdiff_t>(from), buffer.begin() + static_cast<std::ptrdiff_t>(to));
  ++picturesSent;
  for (auto &[pictureTag, picture] : mpegVideoDecode(packet.data(), packet.size(), tag)) {
    ++picturesDecoded;
    latestPicture = picture;
    if (pictureTag < 0) continue;
    frameBuffers[pictureTag] = picture;
    if (frameBuffers.size() > maxFrameBuffers) frameBuffers.erase(frameBuffers.begin());
  }
}

bool isStartCode(std::size_t i) {
  return i + 3 < buffer.size() && buffer[i] == 0 && buffer[i + 1] == 0 && buffer[i + 2] == 1;
}

bool searchedCode(uint8_t code) {
  if (sequenceOnly) return code == sequenceCode;
  if (code == 0x01) return sliceStop;
  return code < 0x01 || code > 0xaf;
}

/* PES_TS: the PTS of the PES packet in which the picture at buffer index INDEX starts */
void associateTimestamp(std::size_t index) {
  uint64_t position = esBytes - buffer.size() + index;
  timestampValid = false;
  while (!timestamps.empty() && timestamps.front().first <= position) {
    timestampValid = true;
    timestamp = timestamps.front().second;
    timestamps.pop_front();
  }
}

void runSearch() {
  if (!searching) return;
  for (; searchPosition + 3 < buffer.size(); ++searchPosition) {
    if (!isStartCode(searchPosition) || !searchedCode(buffer[searchPosition + 3])) continue;
    if (buffer[searchPosition + 3] == sequenceCode) sequenceOnly = false;
    if (buffer[searchPosition + 3] == pictureCode) associateTimestamp(searchPosition);
    headerPosition = esBytes - buffer.size() + searchPosition + 3;
    searchPosition += 4;
    searching = false;
    foundStartCode = true;
    return;
  }
}

/* scanning resumes after the last start code found; header FIFO reads do not move it */
void startSearch() {
  searching = true;
  foundStartCode = false;
  runSearch();
}

/* index of the next picture start code at or after FROM (or, unless PICTURE_ONLY, of the next
 * code that ends a picture: picture, sequence header or end, GOP), or notFound */
std::size_t nextPictureLevel(std::size_t from, bool pictureOnly) {
  for (std::size_t i = from; i + 3 < buffer.size(); ++i) {
    if (!isStartCode(i)) continue;
    uint8_t code = buffer[i + 3];
    if (code == pictureCode) return i;
    if (!pictureOnly && (code == sequenceCode || code == 0xb7 || code == 0xb8)) return i;
  }
  return notFound;
}

/* the next header FIFO byte (0 when none is there yet) */
uint8_t headerByte() {
  uint64_t front = esBytes - buffer.size();
  if (headerPosition >= esBytes) return 0;
  uint64_t at = headerPosition++;
  if (at >= front) return buffer[static_cast<std::size_t>(at - front)];
  if (front - at <= consumed.size()) return consumed[consumed.size() - static_cast<std::size_t>(front - at)];
  return 0;
}

/* removes N bytes consumed by the pipeline */
void consume(std::size_t count) {
  consumed.insert(consumed.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(count));
  if (consumed.size() > consumedKept)
    consumed.erase(consumed.begin(), consumed.begin() + static_cast<std::ptrdiff_t>(consumed.size() - consumedKept));
  buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(count));
  searchPosition = searchPosition > count ? searchPosition - count : 0;
}

/* tries to finish the running task; returns true when the pipeline became idle */
bool advancePipeline() {
  if (pipeline == Pipeline::SeekSequence) {
    std::size_t sequence = notFound;
    for (std::size_t i = 0; i + 3 < buffer.size(); ++i) {
      if (isStartCode(i) && buffer[i + 3] == sequenceCode) {
        sequence = i;
        break;
      }
    }
    if (sequence == notFound) return false;
    std::size_t picture = nextPictureLevel(sequence + 4, true);
    if (picture == notFound) return false;
    pendingHeaders.assign(buffer.begin() + static_cast<std::ptrdiff_t>(sequence),
                          buffer.begin() + static_cast<std::ptrdiff_t>(picture));
    consume(picture);
    return true;
  }
  // decode (after any skipped pictures) from the next picture start code up to the
  // following picture-level start code
  std::size_t position = nextPictureLevel(0, true);
  if (position == notFound) return false;
  std::vector<std::pair<std::size_t, std::size_t>> pictures;
  std::size_t chunk = 0;
  for (int picture = 0; picture <= skips; ++picture) {
    std::size_t end = nextPictureLevel(position + 4, false);
    if (end == notFound) return false;
    pictures.emplace_back(chunk, end);
    if (picture == skips) {
      for (std::size_t i = 0; i < pictures.size(); ++i)
        decodePicture(pictures[i].first, pictures[i].second, i + 1 == pictures.size() && !skipOnly ? taskTag : -1);
      consume(end);
      return true;
    }
    chunk = end;
    position = nextPictureLevel(end, true);
    if (position == notFound) return false;
  }
  return false;
}

void launchTask() {
  uint8_t control = sti5512VideoRegister(0x02);
  uint8_t skip = (task >> 4) & 3;
  /* SKP = 11 (skip one picture, then stop) is a task of its own, also without EXE */
  if (!(control & controlEnable) || taskEnd || (!(task & taskExecute) && skip != 3)) return;
  skipOnly = skip == 3;
  skips = skipOnly ? 0 : skip;
  task &= static_cast<uint8_t>(~(taskExecute | taskForce));
  taskTag = pointerRegister(0x0e);
  ++tasksLaunched;
  if (trace)
    std::fprintf(stderr, "VIDEO task %llu at %.3f s: TIS 0x%02x skips %u, RFP 0x%04x, %s, bit buffer %zu\n",
                 static_cast<unsigned long long>(tasksLaunched), static_cast<double>(elapsedCpuCycles()) / CPU_CLOCK, task, skips, static_cast<unsigned>(taskTag),
                 pipeline == Pipeline::SeekSequence ? "seek sequence" : "decode", buffer.size());
  if (pipeline == Pipeline::Idle) pipeline = Pipeline::Decoding;
  taskEnd = elapsedCpuCycles() + taskCycles;
  raiseSti5512VideoInterrupt(statusPsd);
  startSearch();
}

void softReset() {
  if (trace) std::fprintf(stderr, "VIDEO soft reset\n");
  buffer.clear();
  stalled = false;
  taskEnd = 0; // a running task is abandoned
  headerPosition = 0;
  searchPosition = 0;
  consumed.clear();
  searching = foundStartCode = false;
  sequenceOnly = true;
  pipeline = Pipeline::SeekSequence;
  task = skips = 0;
  pes.reset();
  esBytes = 0;
  timestamps.clear();
  timestampValid = false;
  pendingHeaders.clear();
  mpegVideoReset();
}

uint32_t level() {
  return static_cast<uint32_t>(buffer.size() / 256);
}

uint32_t threshold() {
  return static_cast<uint32_t>(sti5512VideoRegister(0x1a)) << 8 | sti5512VideoRegister(0x1b);
}

std::size_t capacity() {
  uint32_t begin = static_cast<uint32_t>(sti5512VideoRegister(0x14)) << 8 | sti5512VideoRegister(0x15);
  uint32_t top = static_cast<uint32_t>(sti5512VideoRegister(0x18)) << 8 | sti5512VideoRegister(0x19);
  return top > begin ? (top - begin + 1) * 256U : 0x100000U;
}

uint32_t currentStatus() {
  uint32_t value = statusBmi;
  if (pipeline != Pipeline::Decoding) value |= statusPii | statusDei;
  if (foundStartCode) value |= statusSch;
  std::size_t available = !searching && headerPosition < esBytes ? static_cast<std::size_t>(esBytes - headerPosition) : 0;
  if (available == 0) value |= statusHfe;
  if (available >= headerFifoFull) value |= statusHff;
  if (level() == 0) value |= statusBbe;
  if (level() >= threshold()) value |= statusBbf;
  return value;
}

void updateStatus() {
  uint32_t current = currentStatus();
  raiseSti5512VideoInterrupt(current & ~status);
  status = current;
}
}

void resetSti5512VideoDecoder() {
  softReset();
  frameBuffers.clear();
  latestPicture = displayedPicture = nullptr;
  sliceStop = false;
  taskEnd = 0;
  status = currentStatus();
}

bool sti5512VideoInput(uint8_t byte) {
  if (buffer.size() + 4 > capacity()) return false;
  uint8_t out[4];
  ++inputBytes;
  std::size_t count = pes.feed(byte, out);
  uint64_t pts = 0;
  if (pes.takePts(&pts)) {
    timestamps.emplace_back(esBytes + count, pts);
    if (timestamps.size() > 64) timestamps.pop_front();
  }
  if (count == 0) return true;
  buffer.insert(buffer.end(), out, out + count);
  esBytes += count;
  stalled = false;
  runSearch();
  return true;
}

bool sti5512VideoDecoderRead(uint32_t offset, uint8_t *value) {
  switch (offset) {
    case 0x16: *value = static_cast<uint8_t>(level() >> 8 & 0x7f); return true;
    case 0x17: *value = static_cast<uint8_t>(level()); return true;
    case 0x1e: *value = static_cast<uint8_t>(sti5512AudioLevel() >> 8 & 0x3f); return true;
    case 0x1f: *value = static_cast<uint8_t>(sti5512AudioLevel()); return true;
    case 0x3b: *value = static_cast<uint8_t>(currentStatus() >> 16); return true;
    case 0x64: *value = static_cast<uint8_t>(currentStatus() >> 8); return true;
    case 0x65: *value = static_cast<uint8_t>(currentStatus()); return true;
    case 0x66:
      *value = !searching ? headerByte() : 0;
      foundStartCode = false;
      return true;
    case 0x69: *value = startOnMsb; return true;
    case 0x49: *value = static_cast<uint8_t>(timestamp); return true;
    case 0x4a: *value = static_cast<uint8_t>(timestamp >> 8); return true;
    case 0x4b: *value = static_cast<uint8_t>(timestamp >> 16); return true;
    case 0x4c: *value = static_cast<uint8_t>(timestamp >> 24); return true;
    case 0x4d: *value = static_cast<uint8_t>((timestamp >> 32 & 1) | (timestampValid ? 2 : 0)); return true;
    default: return false;
  }
}

void sti5512VideoDecoderWrite(uint32_t offset, uint8_t value) {
  switch (offset) {
    case 0x02:
      if (value & controlSoftReset) softReset();
      if ((value & controlPipeReset) && pipeline == Pipeline::Decoding) {
        pipeline = Pipeline::Idle;
        taskEnd = 0;
        stalled = false;
      }
      break;
    case 0x03:
      task = value;
      if ((value & (taskExecute | taskForce)) == (taskExecute | taskForce)) launchTask();
      break;
    case 0x39:
      if (value & 1) softReset();
      break;
    case 0x44: sti5512VideoInput(value); break;
    case 0x69:
      sliceStop = (value & searchSlice) != 0;
      if (value & searchHeader) startSearch();
      break;
    default: break;
  }
  updateStatus();
}

void sti5512VideoDecoderVsync() {
  if (!(sti5512VideoRegister(0x74) & 0x01)) { // VID_DCF.FRZ
    auto shown = frameBuffers.find(pointerRegister(0x0c));
    displayedPicture = shown != frameBuffers.end() ? shown->second : latestPicture;
  }
  launchTask();
}

std::string sti5512VideoStatus() {
  return std::format("video decoder: VID_CTL=0x{:02x} DCF=0x{:02x}{:02x} MWV=0x{:02x} RFP=0x{:04x} DFP=0x{:04x}, "
                     "{} input bytes ({}), bit buffer {} bytes, {} tasks, {} pictures sent, {} decoded{}, "
                     "{} frame buffers, showing {}",
                     sti5512VideoRegister(0x02), sti5512VideoRegister(0x74), sti5512VideoRegister(0x75),
                     sti5512VideoRegister(0x9b), pointerRegister(0x0e), pointerRegister(0x0c), inputBytes,
                     pes.inPesMode() ? "PES" : "ES", buffer.size(), tasksLaunched, picturesSent, picturesDecoded,
                     mpegDecodingAvailable() ? "" : " (no libavcodec)", frameBuffers.size(),
                     sti5512DisplayedPicture() ? "a picture" : "black");
}

std::shared_ptr<const VideoPicture> sti5512DisplayedPicture() {
  if (!(sti5512VideoRegister(0x75) & 0x20)) return nullptr; // VID_DCF.EVD
  return displayedPicture;
}

void tickSti5512VideoDecoder() {
  if (pipeline == Pipeline::Decoding || pipeline == Pipeline::SeekSequence) {
    if (taskEnd && !stalled && elapsedCpuCycles() >= taskEnd) {
      if (advancePipeline()) {
        pipeline = Pipeline::Idle;
        taskEnd = 0;
        if (trace) std::fprintf(stderr, "VIDEO task done, bit buffer %zu\n", buffer.size());
      } else {
        stalled = true;
        if (trace) std::fprintf(stderr, "VIDEO task waits for data, bit buffer %zu\n", buffer.size());
      }
    }
  }
  updateStatus();
}
