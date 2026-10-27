#include "STi5512_VideoDecoder.h"
#include "STi5512_RegisterBlocks.h"
#include "../../core/cpu/omr.h"
#include "../../core/cpu/st20.h"

#include <cstddef>
#include <cstdint>
#include <deque>

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
 * (up to the next picture, GOP or sequence start code), skipping VID_TIS.SKP pictures first. The pipeline
 * then becomes idle (PII, DEI). Reconstruction is not modelled: frame buffers are not written.
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
std::size_t headerPosition = 0;  // next byte the header FIFO returns
std::size_t searchPosition = 0;  // where a running search resumes
bool searching = false;
bool foundStartCode = false;
bool sequenceOnly = true;
bool sliceStop = false;
Pipeline pipeline = Pipeline::SeekSequence;
uint8_t task = 0;
uint8_t skips = 0;
uint64_t taskEnd = 0;
bool stalled = false; // the running task waits for more data
uint32_t status = 0;

bool isStartCode(std::size_t i) {
  return i + 3 < buffer.size() && buffer[i] == 0 && buffer[i + 1] == 0 && buffer[i + 2] == 1;
}

bool searchedCode(uint8_t code) {
  if (sequenceOnly) return code == sequenceCode;
  if (code == 0x01) return sliceStop;
  return code < 0x01 || code > 0xaf;
}

void runSearch() {
  if (!searching) return;
  for (; searchPosition + 3 < buffer.size(); ++searchPosition) {
    if (!isStartCode(searchPosition) || !searchedCode(buffer[searchPosition + 3])) continue;
    if (buffer[searchPosition + 3] == sequenceCode) sequenceOnly = false;
    headerPosition = searchPosition + 3;
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

/* removes N bytes consumed by the pipeline */
void consume(std::size_t count) {
  buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(count));
  headerPosition = headerPosition > count ? headerPosition - count : 0;
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
    consume(picture);
    return true;
  }
  // decode (after any skipped pictures) from the next picture start code up to the
  // following picture-level start code
  std::size_t position = nextPictureLevel(0, true);
  if (position == notFound) return false;
  for (int picture = 0; picture <= skips; ++picture) {
    std::size_t end = nextPictureLevel(position + 4, false);
    if (end == notFound) return false;
    if (picture == skips) {
      consume(end);
      return true;
    }
    position = nextPictureLevel(end, true);
    if (position == notFound) return false;
  }
  return false;
}

void launchTask() {
  uint8_t control = sti5512VideoRegister(0x02);
  if (!(control & controlEnable) || taskEnd || !(task & taskExecute)) return;
  skips = (task >> 4) & 3;
  if (skips == 3) skips = 1;
  task &= static_cast<uint8_t>(~(taskExecute | taskForce));
  if (pipeline == Pipeline::Idle) pipeline = Pipeline::Decoding;
  taskEnd = elapsedCpuCycles() + taskCycles;
  raiseSti5512VideoInterrupt(statusPsd);
  startSearch();
}

void softReset() {
  buffer.clear();
  stalled = false;
  headerPosition = searchPosition = 0;
  searching = foundStartCode = false;
  sequenceOnly = true;
  pipeline = Pipeline::SeekSequence;
  task = skips = 0;
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
  std::size_t available = !searching && headerPosition < buffer.size() ? buffer.size() - headerPosition : 0;
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
  sliceStop = false;
  taskEnd = 0;
  status = currentStatus();
}

bool sti5512VideoInput(uint8_t byte) {
  if (buffer.size() >= capacity()) return false;
  buffer.push_back(byte);
  stalled = false;
  runSearch();
  return true;
}

bool sti5512VideoDecoderRead(uint32_t offset, uint8_t *value) {
  switch (offset) {
    case 0x16: *value = static_cast<uint8_t>(level() >> 8 & 0x7f); return true;
    case 0x17: *value = static_cast<uint8_t>(level()); return true;
    case 0x3b: *value = static_cast<uint8_t>(currentStatus() >> 16); return true;
    case 0x64: *value = static_cast<uint8_t>(currentStatus() >> 8); return true;
    case 0x65: *value = static_cast<uint8_t>(currentStatus()); return true;
    case 0x66:
      *value = !searching && headerPosition < buffer.size() ? buffer[headerPosition++] : 0;
      foundStartCode = false;
      return true;
    case 0x69: *value = startOnMsb; return true;
    default: return false;
  }
}

void sti5512VideoDecoderWrite(uint32_t offset, uint8_t value) {
  switch (offset) {
    case 0x02:
      if (value & controlSoftReset) softReset();
      if ((value & controlPipeReset) && pipeline == Pipeline::Decoding) pipeline = Pipeline::Idle;
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
  launchTask();
}

void tickSti5512VideoDecoder() {
  if (pipeline == Pipeline::Decoding || pipeline == Pipeline::SeekSequence) {
    if (taskEnd && !stalled && elapsedCpuCycles() >= taskEnd) {
      if (advancePipeline()) {
        pipeline = Pipeline::Idle;
        taskEnd = 0;
      } else {
        stalled = true;
      }
    }
  }
  updateStatus();
}
