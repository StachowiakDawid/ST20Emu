#include "Dsr9500Frontend.h"

#include "../core/cpu/omr.h"
#include "../core/cpu/st20.h"
#include "../core/memory/memory.h"
#include "../soc/sti5512/STi5512_Pti.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iterator>
#include <vector>

/*
 * What the DSR9500 main application does with the front end (Decompilation/DSR9500
 * a_7fe81640.c, a_7fe84150.c, a_7fe9c528.c) and how this model answers:
 *
 * Board latch 0x50400000 (write-only byte, shadowed by the firmware):
 *   bit 7  LNB supply on                     (f_7fe9c6d4)
 *   bit 5  1 = 18 V (horizontal), 0 = 13 V   (f_7fe9c680: vertical clears it)
 *   bit 3  0 = 22 kHz tone on (high band)    (f_7fe9c728)
 *   bit 2  HM1221 reset, active low          (f_7fe9c62c; pulsed 1-0-1 before the ID check)
 * The latch at 0x50200000 is stored only. Releasing the reset (bit 2 rising) clears the chip.
 *
 * LNB: a universal LNB, local oscillator 9750 MHz without and 10600 MHz with the tone. A
 * transponder is visible when the supply is on, its polarisation matches the voltage and its
 * band (LO below 11700 MHz unless given) matches the tone; its IF is FREQ - LO.
 *
 * HM1221 registers (I2C 0xaa, register pointer then data; every other register is storage):
 *   0x00       identity 0x5a (qpsk_reset accepts the chip on it)
 *   0x01-0x03  symbol rate word X, 0x07 its range B (0-3): SR = X / K[B] kSym/s with
 *              K = 737.468148, 553.101111, 414.825833, 368.734074 (inverse of f_7fe821b0)
 *   0x18       enabled code rates, bit N = rate N/(N+1); FEC lock needs the bit of the
 *              transponder's rate (3/4 unless given; 0xae, the firmware's "auto", enables
 *              1/2, 2/3, 3/4, 5/6 and 7/8)
 *   0x6d/0x72  3-wire tuner port: 0x6d with bit 1 clear starts a word, three writes to 0x72
 *              shift its bytes, 0x6d with bit 1 set loads it. Reads of 0x6d return bits 7:6
 *              = 01 (port ready). PLL words: a band word (first byte 0) whose second byte
 *              bit 4 clear selects the half-step (low band) prescaler, and a divider word
 *              (first byte 0xd0-0xff, bit 3 set): ratio index R = 0xf - (byte0 >> 4),
 *              M = byte1 | (byte0 bit 0) << 8, A = byte2 >> 3 (0 = 32), N = 32 M + A;
 *              IF = N * 1011.1 kHz / 2^R (halved in the low band) - the firmware's VCO tables
 *              at ROM 0x7ff2b0fc/0x7ff2b114. Other words (loop filter set-up) are ignored.
 *   0x66       bit 5: PLL locked (a divider word put the IF in 900-2200 MHz)
 *   0x08       bit 5 carrier lock, bit 6 FEC lock (the firmware wants both: 0x60)
 *   0x22       low nibble: code rate N (3 = 3/4) while locked, 8 otherwise
 *   0x55       bits 4:3 = 11 while locked (tracking loops settled)
 *   0x47/0x48  carrier offset N (12 bits, 0x47 = N[11:4], 0x48 bits 3:0 = N[3:0]) with
 *              offset = N * SR / 16384 kHz = PLL - true IF (the firmware subtracts it)
 *   0x10       bit 6: error count ready while locked (writes 0x30/0x70 restart it)
 *   0x12/0x13  error count (low/high): 3 per measurement period while locked
 *   0x27       quality: firmware shows 40 - (signed)reg; 0xc4 (100) locked, 0x23 (5) not
 *   0x6a       write 0x84 starts a level measurement; reads have bit 7 clear (done)
 *   0x68/0x69  level word L: the firmware shows 250 - L / 100; 11000 (140) while locked
 *   0x76       bit 6 set: DiSEqC/tone generator idle
 * Lock is immediate once frequency (within 1.5 MHz), symbol rate (within 1 %), code rate,
 * LNB voltage, tone and supply all match a transponder. DiSEqC messages are not decoded.
 *
 * Transport stream: each transponder broadcasts its file in a loop at the useful bit rate of
 * DVB-S QPSK at its code rate, SR * 2 * N/(N+1) * 188/204 bit/s, paced by the emulated CPU clock; a
 * locked demodulator hands the packets due to the PTI input interface.
 */
namespace {
struct Transponder {
  std::string path;
  double frequencyMhz = 0;
  uint32_t symbolRate = 0; // kSym/s
  bool horizontal = false;
  bool highBand = false;
  int codeRate = 3;             // N for rate N/(N+1)
  std::vector<uint8_t> packets; // whole 188-byte packets
};

constexpr uint8_t identity = 0x5a;
constexpr uint32_t latch0Address = 0x50200000U;
constexpr uint32_t latch1Address = 0x50400000U;
constexpr uint8_t latchLnbPower = 0x80;
constexpr uint8_t latchHorizontal = 0x20;
constexpr uint8_t latchToneOff = 0x08;
constexpr uint8_t latchDemodRun = 0x04;
constexpr double lowLo = 9750.0;
constexpr double highLo = 10600.0;
constexpr double bandSplit = 11700.0;
constexpr double captureKhz = 1500.0;
constexpr double vcoStepKhz = 1011.1;
constexpr uint16_t lockedLevelWord = 11000;
constexpr std::size_t packetSize = 188;
constexpr uint64_t maxBurstPackets = 4096;

I2cRegisterDevice chip{0xaa};
std::vector<Transponder> transponders;
std::vector<uint8_t> shift;
bool lowBandPrescaler = false;
double pllKhz = 0;
bool pllLocked = false;
uint8_t latch0 = 0;
uint8_t latch1 = 0;
bool latchesRegistered = false;
const Transponder *streaming = nullptr;
uint64_t nextPacket = 0;
uint64_t delivered = 0;
/* ST20_FRONTEND_TRACE=1: print latch writes, demodulator register writes and lock changes */
const bool trace = std::getenv("ST20_FRONTEND_TRACE") != nullptr;
bool tracedLock = false;

void resetChip() {
  chip.registers = {};
  chip.registers[0] = identity;
  shift.clear();
  lowBandPrescaler = false;
  pllKhz = 0;
  pllLocked = false;
}

double symbolRateKsps() {
  static constexpr double factors[4] = {737.468148, 553.101111, 414.825833, 368.734074};
  uint32_t word = static_cast<uint32_t>(chip.registers[1]) << 16 | static_cast<uint32_t>(chip.registers[2]) << 8 |
                  chip.registers[3];
  return word / factors[chip.registers[7] & 3];
}

struct Signal {
  const Transponder *transponder = nullptr;
  bool carrier = false;
  bool locked = false;
  double offsetKhz = 0;
};

Signal currentSignal() {
  Signal signal;
  if (!(latch1 & latchLnbPower) || !(latch1 & latchDemodRun) || !pllLocked) return signal;
  bool horizontal = (latch1 & latchHorizontal) != 0;
  bool tone = !(latch1 & latchToneOff);
  for (const Transponder &tp : transponders) {
    if (tp.horizontal != horizontal || tp.highBand != tone) continue;
    double ifKhz = (tp.frequencyMhz - (tp.highBand ? highLo : lowLo)) * 1000.0;
    if (ifKhz < 950000.0 || ifKhz > 2150000.0) continue;
    double offset = pllKhz - ifKhz;
    if (std::fabs(offset) > captureKhz) continue;
    if (std::fabs(symbolRateKsps() - tp.symbolRate) > tp.symbolRate * 0.01 + 1.0) continue;
    signal.transponder = &tp;
    signal.carrier = true;
    signal.locked = (chip.registers[0x18] & (1U << tp.codeRate)) != 0;
    signal.offsetKhz = offset;
    return signal;
  }
  return signal;
}

void loadPllWord(const uint8_t *word) {
  if (word[0] == 0) {
    lowBandPrescaler = (word[1] & 0x10) == 0;
    return;
  }
  if (word[0] < 0xd0 || !(word[0] & 0x08)) return; // loop filter and other set-up words
  int ratio = 0xf - (word[0] >> 4);
  uint32_t m = word[1] | static_cast<uint32_t>(word[0] & 1) << 8;
  uint32_t a = word[2] >> 3;
  if (a == 0) a = 32;
  double step = vcoStepKhz / (1 << ratio) / (lowBandPrescaler ? 2.0 : 1.0);
  pllKhz = (32 * m + a) * step;
  pllLocked = pllKhz >= 900000.0 && pllKhz <= 2200000.0;
}

void writeHook(void *, uint8_t reg, uint8_t value) {
  if (trace) std::fprintf(stderr, "FRONTEND demod[%02x] = %02x @%08x\n", reg, value, get_iptr());
  switch (reg) {
    case 0x6d:
      if (!(value & 2)) {
        shift.clear();
      } else {
        if (shift.size() >= 3) loadPllWord(shift.data() + shift.size() - 3);
        shift.clear();
      }
      break;
    case 0x72: shift.push_back(value); break;
    default: break;
  }
}

uint8_t readHook(void *, uint8_t reg, uint8_t stored) {
  Signal signal = currentSignal();
  int offset = 0;
  if (signal.carrier) {
    double sr = std::max(symbolRateKsps(), 1.0);
    offset = std::clamp(static_cast<int>(std::lround(signal.offsetKhz * 16384.0 / sr)), -2048, 2047);
  }
  switch (reg) {
    case 0x00: return identity;
    case 0x08: return static_cast<uint8_t>((stored & 0x9f) | (signal.carrier ? 0x20 : 0) | (signal.locked ? 0x40 : 0));
    case 0x10: return static_cast<uint8_t>((stored & ~0x40) | (signal.locked ? 0x40 : 0));
    case 0x12: return signal.locked ? 3 : 0;
    case 0x13: return 0;
    case 0x22: return signal.locked ? static_cast<uint8_t>(signal.transponder->codeRate) : 0x08;
    case 0x27: return signal.locked ? 0xc4 : 0x23;
    case 0x47: return static_cast<uint8_t>((offset >> 4) & 0xff);
    case 0x48: return static_cast<uint8_t>((stored & 0xf0) | (offset & 0x0f));
    case 0x55: return static_cast<uint8_t>((stored & ~0x18) | (signal.locked ? 0x18 : 0));
    case 0x66: return static_cast<uint8_t>((stored & ~0x20) | (pllLocked ? 0x20 : 0));
    case 0x68: return signal.locked ? static_cast<uint8_t>(lockedLevelWord >> 8) : 0xff;
    case 0x69: return signal.locked ? static_cast<uint8_t>(lockedLevelWord & 0xff) : 0xff;
    case 0x6a: return static_cast<uint8_t>(stored & 0x7f);
    case 0x6d: return static_cast<uint8_t>((stored & 0x3f) | 0x40);
    case 0x76: return static_cast<uint8_t>(stored | 0x40);
    default: return stored;
  }
}

int readLatch(void *context, uint32_t, int, uint32_t *value) {
  *value = *static_cast<uint8_t *>(context);
  return 0;
}

int writeLatch(void *context, uint32_t address, int, uint32_t value) {
  auto *latch = static_cast<uint8_t *>(context);
  uint8_t previous = *latch;
  *latch = static_cast<uint8_t>(value);
  if (trace) std::fprintf(stderr, "FRONTEND latch %08x = %02x @%08x\n", address, *latch, get_iptr());
  if (address >= latch1Address && address < latch1Address + 4 && !(previous & latchDemodRun) && (*latch & latchDemodRun))
    resetChip();
  return 0;
}

uint64_t packetIndexNow(const Transponder &tp) {
  /* packets per second: SR * 1000 * 2 * N/(N+1) * 188/204 / (188 * 8) = SR * 2000 N / (1632 (N+1)) */
  unsigned __int128 numerator = static_cast<unsigned __int128>(elapsedCpuCycles()) * tp.symbolRate * 2000U *
                                static_cast<unsigned>(tp.codeRate);
  return static_cast<uint64_t>(numerator / (static_cast<unsigned __int128>(1632U) * static_cast<unsigned>(tp.codeRate + 1) * CPU_CLOCK));
}

bool parseDouble(std::string_view text, double *value) {
  std::string copy(text);
  char *end = nullptr;
  *value = std::strtod(copy.c_str(), &end);
  return !copy.empty() && end == copy.c_str() + copy.size();
}
} // namespace

std::string dsr9500AddTransponder(std::string_view spec) {
  /* FILE may itself contain ':'; parameters are taken from the end */
  std::vector<std::string_view> parts;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= spec.size(); ++i) {
    if (i == spec.size() || spec[i] == ':') {
      parts.push_back(spec.substr(start, i - start));
      start = i + 1;
    }
  }
  Transponder tp;
  std::string_view band;
  while (parts.size() > 4) {
    std::string_view last = parts.back();
    if (last == "LO" || last == "HI" || last == "lo" || last == "hi") {
      band = last;
    } else if (last.size() == 3 && last[1] == '/' && last[0] >= '1' && last[0] <= '7' && last[2] == last[0] + 1) {
      tp.codeRate = last[0] - '0';
    } else {
      break;
    }
    parts.pop_back();
  }
  if (parts.size() < 4)
    return std::format("invalid transponder {} (expected FILE.ts:FREQ_MHZ:SR_KSPS:H|V[:LO|HI][:FEC])", spec);
  std::size_t first = parts.size() - 3;
  std::size_t pathEnd = static_cast<std::size_t>(parts[first].data() - spec.data()) - 1;
  tp.path = std::string(spec.substr(0, pathEnd));
  if (!parseDouble(parts[first], &tp.frequencyMhz) || tp.frequencyMhz < 10000 || tp.frequencyMhz > 13000)
    return std::format("invalid transponder frequency {} MHz (expected 10700-12750)", parts[first]);
  auto [end, error] = std::from_chars(parts[first + 1].data(), parts[first + 1].data() + parts[first + 1].size(), tp.symbolRate);
  if (error != std::errc{} || end != parts[first + 1].data() + parts[first + 1].size() || tp.symbolRate < 1000 || tp.symbolRate > 45000)
    return std::format("invalid symbol rate {} kSym/s (expected 1000-45000)", parts[first + 1]);
  std::string_view polarisation = parts[first + 2];
  if (polarisation == "H" || polarisation == "h") tp.horizontal = true;
  else if (polarisation != "V" && polarisation != "v") return std::format("invalid polarisation {} (expected H or V)", polarisation);
  tp.highBand = !band.empty() ? (band == "HI" || band == "hi") : tp.frequencyMhz >= bandSplit;
  double ifMhz = tp.frequencyMhz - (tp.highBand ? highLo : lowLo);
  if (ifMhz < 950 || ifMhz > 2150)
    return std::format("transponder {} MHz is outside the {} band of a universal LNB (IF {} MHz)", tp.frequencyMhz,
                       tp.highBand ? "high" : "low", ifMhz);
  std::ifstream file(tp.path, std::ios::binary);
  if (!file) return "cannot open " + tp.path;
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  std::size_t sync = 0;
  while (sync < packetSize && sync + packetSize * 2 < data.size() &&
         !(data[sync] == 0x47 && data[sync + packetSize] == 0x47 && data[sync + 2 * packetSize] == 0x47))
    ++sync;
  if (sync >= packetSize || data.size() < sync + packetSize) return tp.path + " is not an MPEG transport stream";
  for (std::size_t i = sync; i + packetSize <= data.size(); i += packetSize) {
    if (data[i] != 0x47) continue;
    tp.packets.insert(tp.packets.end(), data.begin() + static_cast<std::ptrdiff_t>(i),
                      data.begin() + static_cast<std::ptrdiff_t>(i + packetSize));
  }
  transponders.push_back(std::move(tp));
  streaming = nullptr;
  return {};
}

I2cRegisterDevice &dsr9500QpskDevice() {
  return chip;
}

int initDsr9500Frontend() {
  resetI2cDevice(chip);
  chip.writeHook = writeHook;
  chip.readHook = readHook;
  resetChip();
  latch0 = latch1 = 0;
  streaming = nullptr;
  delivered = 0;
  if (!latchesRegistered) {
    int result = registerMemoryDevice({latch0Address, latch0Address + 3, &latch0, readLatch, writeLatch});
    if (!result) result = registerMemoryDevice({latch1Address, latch1Address + 3, &latch1, readLatch, writeLatch});
    if (result) return result;
    latchesRegistered = true;
  }
  return 0;
}

void pollDsr9500Frontend() {
  if (transponders.empty()) return;
  Signal signal = currentSignal();
  if (trace && signal.locked != tracedLock) {
    tracedLock = signal.locked;
    std::fprintf(stderr, "FRONTEND %s\n", dsr9500FrontendStatus().c_str());
  }
  if (!signal.locked) {
    streaming = nullptr;
    return;
  }
  const Transponder &tp = *signal.transponder;
  uint64_t now = packetIndexNow(tp);
  if (streaming != &tp) {
    streaming = &tp;
    nextPacket = now;
    return;
  }
  if (now - nextPacket > maxBurstPackets) nextPacket = now - maxBurstPackets;
  std::size_t count = tp.packets.size() / packetSize;
  for (; nextPacket < now; ++nextPacket) {
    sti5512PtiInput(tp.packets.data() + (nextPacket % count) * packetSize);
    ++delivered;
  }
}

std::string dsr9500FrontendStatus() {
  Signal signal = currentSignal();
  std::string lnb = !(latch1 & latchLnbPower) ? "LNB off"
                                               : std::format("LNB {} V, 22 kHz {}", (latch1 & latchHorizontal) ? 18 : 13,
                                                             (latch1 & latchToneOff) ? "off" : "on");
  std::string tuned = pllLocked ? std::format("PLL {:.1f} MHz", pllKhz / 1000.0) : "PLL unlocked";
  std::string lock = signal.locked ? std::format("locked to {} ({} MHz {} {})", signal.transponder->path,
                                                 signal.transponder->frequencyMhz, signal.transponder->symbolRate,
                                                 signal.transponder->horizontal ? 'H' : 'V')
                     : signal.carrier ? "carrier, no FEC lock"
                                      : "no signal";
  return std::format("{}, {}, SR {:.0f} kSym/s, {}; {} packets delivered, {} accepted by the PTI, {} matched a slot, {} units dropped (ring full)",
                     lnb, tuned, symbolRateKsps(), lock, delivered, sti5512PtiInputPackets(), sti5512PtiMatchedPackets(),
                     sti5512PtiDroppedUnits());
}

bool dsr9500FrontendLocked() {
  return currentSignal().locked;
}

uint64_t dsr9500FrontendPackets() {
  return delivered;
}
