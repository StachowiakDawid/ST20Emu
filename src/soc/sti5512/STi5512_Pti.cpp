#include "STi5512_Pti.h"
#include "STi5512_AudioDecoder.h"
#include "STi5512_PtiDma.h"
#include "STi5512_VideoDecoder.h"
#include "../common/STi55xx_Asc.h"
#include "../../core/cpu/omr.h"
#include "../../core/cpu/st20.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <vector>

/*
 * STi5512 Register Manual (7145562 B) chapter 20 and Table 21; the table layout is the one
 * of ST's PTI driver 3.4.1/3.6.0 (Decompilation/DSR9500 m_6be4.c ... m_8df4.c and the main
 * application's a_7fe9342c.c ... a_7fe94784.c), whose TC microcode (TC version 3.6.0) the
 * drivers load into the instruction memory at 0x2003c000. The model does not run that code;
 * it interprets the same tables.
 *
 * Registers (PTIBaseAddress 0x20030000):
 *   0x00-0x0c IntStatus0-3 (16 bits each; set by the TC), 0x10-0x1c IntEnable0-3,
 *   0x20-0x2c IntAck0-3 (write 1 to clear), 0x30 TCMode (the TC runs while bit 0 is set and
 *   bit 1, reset, is clear), 0x40/0x44 AudPTS and 0x48/0x4c VidPTS (read the STC),
 *   0x50/0x54 STCTimer (bit 32 first, the write to 0x50 loads the 90 kHz counter).
 *   The input interface accepts packets while IIFFIFOEnable (0x20032008) bit 0 is set.
 *   Interrupt bits: IntStatus1-3 bit n = slot 16 * (k - 1) + n has new data in its buffer;
 *   IntStatus0 bit 8 = PCR captured.
 *
 * TC data memory (0x20038000, offsets below; pointers inside it are offsets from
 * 0x20030000, i.e. 0x8360 for 0x20038360):
 *   0x000  PID table: 48 half-words, slot 2k in the low half of word k, 2k+1 in the high
 *          half; 0xe000 = unused (never matches a 13-bit PID). Slot 47 is the PCR slot. The
 *          lowest matching slot takes the packet.
 *   0x060  slot descriptors, 16 bytes each:
 *          +0  flags (0x20)
 *          +4  bits 31:16: section state block (0x8680 + 24 n), see below
 *          +8  bits 31:16: DMA descriptor (0x8360 + 20 n); bits 15:0 descrambler (0xf000 none)
 *          +12 configuration: 0x80 disabled (packets dropped), 0x2000 sections, 0x1000 raw
 *              packets, 0x10 signal (interrupt) per completed unit, 0x200 partial PES per
 *              packet instead, bits 3:2 = 1/2/3: payload to DMA channel 1 (video) / 2 (audio)
 *              / 3 (data) without interrupt; neither: PES packets
 *   0x360  DMA descriptors, 20 bytes: base, top (base + size - 16; the buffer ends at
 *          top | 15), read pointer (the CPU's), write pointer (the TC's), unit start
 *   0x680  section state blocks, 24 bytes: word 0 = mask of the section filters (0-31) the
 *          slot uses; the TC's assembly state lives in the model instead of words 1-5
 *   0xa40  low half: PCR PID (0xe000 none)
 *   0xa44  PCR captured with the interrupt: half-words base[32:17] | base[16:1] << 16;
 *   0xa48  low half: base[0] << 15 | extension; high half: STC base[32:17] at arrival
 *   0xa4c  STC base[16:1] | extension << 16 | base[0] << 31
 *   0xa64/0xa68  the running STC in the 0xa44/0xa48 layout, updated with every input packet
 *          (the drivers' pti_get_pcr; STCLKRV_GetSTC takes it as the current time)
 *   0xa6c  section filter enable mask
 *   0xa80  bits 9:4: slot the TC's DMA is working on (63 = none, pti_dma_sync)
 * Section filters (0x20034000 + 16 n): data bits 31:0, mask 31:0, data 63:32, mask 63:32;
 * filter byte 0 (bits 63:56) is compared with section byte 0 (table_id), filter bytes 1-7
 * with section bytes 3-9 (the drivers skip the section_length bytes); a section passes when
 * any filter of the slot's mask that is enabled in 0xa6c matches.
 *
 * Buffer formats written by the model (as the drivers read them): sections back to back;
 * raw 188-byte packets; PES packets each followed by a 4-byte little-endian word holding the
 * packet's start address (pti_extract_pes_packet_data); partial PES records of a flags byte
 * (bit 6 payload unit start, bit 7 discontinuity, bits 3:0 continuity counter), a length
 * byte and the payload. A unit that does not fit in the free space is dropped (overflow).
 * A/V payload goes to the slot's ring and advances PTI_DMAnWrite of its channel (continuing
 * from PTI_DMAnWrite when the channel's registers describe the same ring, so CPU resets of the
 * channel take effect); a ring without buffer (top below base, e.g. base 0x6000) feeds the decoder input directly.
 * Descrambling (scrambled packets pass unchanged), alternate output and the carousel are not
 * modelled.
 */
namespace {
constexpr uint32_t registerBase = 0x20030000U;
constexpr uint32_t registerEnd = 0x20030057U;
constexpr uint32_t tcData = 0x20038000U;
constexpr uint32_t sectionFilters = 0x20034000U;
constexpr uint32_t iifFifoEnable = 0x20032008U;
constexpr uint32_t slotTable = tcData + 0x060;
constexpr uint32_t pcrPidWord = tcData + 0xa40;
constexpr uint32_t pcrCapture = tcData + 0xa44;
constexpr uint32_t stcNow = tcData + 0xa64;
constexpr uint32_t filterEnableWord = tcData + 0xa6c;
constexpr uint32_t dmaSlotWord = tcData + 0xa80;
constexpr int slotCount = 48;
constexpr std::size_t packetSize = 188;

constexpr uint32_t cfgDisabled = 0x80;
constexpr uint32_t cfgSection = 0x2000;
constexpr uint32_t cfgRaw = 0x1000;
constexpr uint32_t cfgSignal = 0x10;
constexpr uint32_t cfgPartial = 0x200;
constexpr uint32_t pcrInterruptBit = 0x100;

std::array<uint32_t, (registerEnd + 1 - registerBase) / 4> registers{};
uint64_t stcBase = 0;     // 90 kHz units at stcLoadCycle
uint64_t stcLoadCycle = 0;
uint64_t inputPackets = 0;
uint64_t matchedPackets = 0;
uint64_t droppedUnits = 0; // units that did not fit in their ring

struct SlotState {
  uint32_t pid = 0xffffffffU;
  uint32_t cfg = 0;
  uint32_t descriptor = 0;
  int continuity = -1;
  bool sectionActive = false;
  std::vector<uint8_t> section;
  bool pesActive = false;
  std::vector<uint8_t> pes;
};
std::array<SlotState, slotCount> slots;
/* ST20_PTI_TRACE=1: print a slot's set-up when its first packet (after a change) arrives */
const bool trace = std::getenv("ST20_PTI_TRACE") != nullptr;

uint32_t word(uint32_t address) {
  uint32_t value = 0;
  readBytes(address, 4, &value);
  return value;
}

void setWord(uint32_t address, uint32_t value) {
  storeBytes(address, 4, value);
}

/* the 27 MHz system time clock */
uint64_t stc27() {
  uint64_t elapsed = elapsedCpuCycles() - stcLoadCycle;
  return stcBase * 300 + static_cast<uint64_t>(static_cast<unsigned __int128>(elapsed) * 27000000U / CPU_CLOCK);
}

void raiseSlot(int slot) {
  registers[1 + static_cast<std::size_t>(slot / 16)] |= 1U << (slot % 16);
}

/* writes DATA into the circular buffer of DMA descriptor DESCRIPTOR; READ is the reader's
 * pointer. Returns false (and writes nothing) when it does not fit. */
bool ringWrite(uint32_t descriptor, const uint8_t *data, std::size_t length, uint32_t read, uint32_t *start) {
  uint32_t base = word(descriptor);
  uint32_t end = word(descriptor + 4) | 0xfU;
  if (base == 0 || end <= base) return false;
  uint32_t size = end - base + 1;
  uint32_t write = word(descriptor + 12);
  if (write < base || write > end) write = base;
  if (read < base || read > end) read = write;
  uint32_t used = (write - read + size) % size;
  if (length > size - 1 - used) {
    ++droppedUnits;
    return false;
  }
  if (start) *start = write;
  for (std::size_t i = 0; i < length; ++i) {
    storeBytes(write, 1, data[i]);
    write = write == end ? base : write + 1;
  }
  setWord(descriptor + 12, write);
  setWord(descriptor + 16, write);
  return true;
}

bool sectionMatches(const std::vector<uint8_t> &section, uint32_t filters) {
  uint8_t bytes[8]{};
  bytes[0] = section[0];
  for (std::size_t i = 1; i < 8; ++i) bytes[i] = i + 2 < section.size() ? section[i + 2] : 0;
  uint32_t high = static_cast<uint32_t>(bytes[0]) << 24 | static_cast<uint32_t>(bytes[1]) << 16 |
                  static_cast<uint32_t>(bytes[2]) << 8 | bytes[3];
  uint32_t low = static_cast<uint32_t>(bytes[4]) << 24 | static_cast<uint32_t>(bytes[5]) << 16 |
                 static_cast<uint32_t>(bytes[6]) << 8 | bytes[7];
  for (uint32_t filter = 0; filter < 32; ++filter) {
    if (!(filters & (1U << filter))) continue;
    uint32_t at = sectionFilters + filter * 16;
    if (((low ^ word(at)) & word(at + 4)) == 0 && ((high ^ word(at + 8)) & word(at + 12)) == 0) return true;
  }
  return false;
}

void completeSection(int slot, uint32_t slotWord1, uint32_t descriptor, const std::vector<uint8_t> &section) {
  uint32_t state = registerBase + (slotWord1 >> 16);
  uint32_t filters = word(state) & word(filterEnableWord);
  if (!filters || !sectionMatches(section, filters)) return;
  if (ringWrite(descriptor, section.data(), section.size(), word(descriptor + 8), nullptr)) raiseSlot(slot);
}

/* appends section bytes and delivers every completed section */
void collectSections(int slot, SlotState &state, uint32_t slotWord1, uint32_t descriptor, const uint8_t *data,
                     std::size_t length) {
  std::vector<uint8_t> &buffer = state.section;
  for (std::size_t i = 0; i < length && state.sectionActive; ++i) {
    if (buffer.empty() && data[i] == 0xff) { // stuffing: no further section in this packet
      state.sectionActive = false;
      break;
    }
    buffer.push_back(data[i]);
    if (buffer.size() < 3) continue;
    std::size_t total = ((static_cast<std::size_t>(buffer[1]) & 0x0f) << 8 | buffer[2]) + 3;
    if (buffer.size() < total) continue;
    completeSection(slot, slotWord1, descriptor, buffer);
    buffer.clear();
  }
}

void finishPes(int slot, SlotState &state, uint32_t descriptor, bool signal) {
  if (!state.pesActive || state.pes.empty()) return;
  state.pesActive = false;
  std::vector<uint8_t> unit = state.pes;
  unit.resize(unit.size() + 4);
  uint32_t start = 0;
  uint32_t base = word(descriptor);
  uint32_t end = word(descriptor + 4) | 0xfU;
  uint32_t write = word(descriptor + 12);
  // the trailer holds the start address, known once the write position is
  if (write < base || write > end) write = base;
  for (int i = 0; i < 4; ++i) unit[state.pes.size() + static_cast<std::size_t>(i)] = static_cast<uint8_t>(write >> (8 * i));
  if (ringWrite(descriptor, unit.data(), unit.size(), word(descriptor + 8), &start) && signal) raiseSlot(slot);
  state.pes.clear();
}

void avPayload(uint32_t channel, uint32_t descriptor, const uint8_t *data, std::size_t length) {
  uint32_t base = word(descriptor);
  uint32_t end = word(descriptor + 4) | 0xfU;
  if (base == 0 || end <= base) { // no buffer: straight into the decoder's CD FIFO
    for (std::size_t i = 0; i < length; ++i) {
      if (channel == 1) sti5512VideoInput(data[i]);
      else if (channel == 2) sti5512AudioInput(data[i]);
    }
    return;
  }
  uint32_t read = sti5512PtiDmaRegister(0x30 + 4 * channel);
  /* the channel's DMA registers describe the same ring: when the CPU has reset them
   * (pti_list_drain, pti_start_list) the TC continues from their write pointer */
  if (sti5512PtiDmaRegister(4 * channel) == base) setWord(descriptor + 12, sti5512PtiDmaRegister(0x20 + 4 * channel));
  if (ringWrite(descriptor, data, length, read, nullptr)) sti5512PtiDmaSetWrite(channel, word(descriptor + 12));
  else if (trace)
    std::fprintf(stderr, "PTI channel %u ring full at %.1f s, payload dropped (DMA base %08x read %08x write %08x enable %x, TC write %08x)\n",
                 channel, static_cast<double>(elapsedCpuCycles()) / CPU_CLOCK, sti5512PtiDmaRegister(4 * channel), read,
                 sti5512PtiDmaRegister(0x20 + 4 * channel), sti5512PtiDmaRegister(0x50), word(descriptor + 12));
}

void capturePcr(const uint8_t *packet) {
  const uint8_t *pcr = packet + 6;
  uint32_t first = static_cast<uint32_t>(pcr[0]) << 8 | pcr[1];
  uint32_t second = static_cast<uint32_t>(pcr[2]) << 8 | pcr[3];
  uint32_t third = static_cast<uint32_t>(pcr[4]) << 8 | pcr[5];
  uint64_t stc = stc27();
  uint64_t base = (stc / 300) & 0x1ffffffffULL;
  uint32_t extension = static_cast<uint32_t>(stc % 300);
  setWord(pcrCapture, first | second << 16);
  setWord(pcrCapture + 4, third | static_cast<uint32_t>(base >> 17) << 16);
  setWord(pcrCapture + 8, static_cast<uint32_t>((base >> 1) & 0xffff) | extension << 16 | static_cast<uint32_t>(base & 1) << 31);
  registers[0] |= pcrInterruptBit;
}

/* the TC keeps the running STC at 0xa64/0xa68 (read by pti_get_pcr, which STCLKRV_GetSTC
 * uses as the current time), in the PCR capture layout */
void publishStc() {
  uint64_t stc = stc27();
  uint64_t base = (stc / 300) & 0x1ffffffffULL;
  uint32_t extension = static_cast<uint32_t>(stc % 300);
  setWord(stcNow, static_cast<uint32_t>(base >> 17) | static_cast<uint32_t>((base >> 1) & 0xffff) << 16);
  setWord(stcNow + 4, (word(stcNow + 4) & 0xffff0000U) | static_cast<uint32_t>(base & 1) << 15 | extension);
}

void processPacket(const uint8_t *packet) {
  if (packet[0] != 0x47 || (packet[1] & 0x80)) return;
  uint32_t pid = static_cast<uint32_t>(packet[1] & 0x1f) << 8 | packet[2];
  bool unitStart = (packet[1] & 0x40) != 0;
  uint32_t adaptation = (packet[3] >> 4) & 3;
  int continuity = packet[3] & 0x0f;
  std::size_t payload = 4;
  if (adaptation & 2) {
    payload = 5 + static_cast<std::size_t>(packet[4]);
    if (payload > packetSize) return;
    if (packet[4] >= 7 && (packet[5] & 0x10) && pid == (word(pcrPidWord) & 0xffff)) capturePcr(packet);
  }
  int slot = -1;
  for (int k = 0; k < slotCount / 2 && slot < 0; ++k) {
    uint32_t pair = word(tcData + static_cast<uint32_t>(k) * 4);
    if ((pair & 0xffff) == pid) slot = 2 * k;
    else if ((pair >> 16) == pid) slot = 2 * k + 1;
  }
  if (slot < 0) return;
  ++matchedPackets;
  uint32_t entry = slotTable + static_cast<uint32_t>(slot) * 16;
  uint32_t word1 = word(entry + 4);
  uint32_t word2 = word(entry + 8);
  uint32_t cfg = word(entry + 12);
  uint32_t descriptor = registerBase + (word2 >> 16);
  SlotState &state = slots[static_cast<std::size_t>(slot)];
  if (state.pid != pid || state.cfg != cfg || state.descriptor != descriptor) {
    if (trace)
      std::fprintf(stderr, "PTI slot %d pid 0x%04x cfg 0x%04x ring 0x%08x-0x%08x filters 0x%08x/0x%08x\n", slot, pid, cfg,
                   word(descriptor), word(descriptor + 4) | 0xfU, word(registerBase + (word1 >> 16)), word(filterEnableWord));
    state = SlotState{};
    state.pid = pid;
    state.cfg = cfg;
    state.descriptor = descriptor;
  }
  if (cfg & cfgDisabled) return;
  setWord(dmaSlotWord, (word(dmaSlotWord) & 0xffff0000U) | static_cast<uint32_t>(slot) << 4);
  bool discontinuity = false;
  if (adaptation & 1) {
    if (state.continuity >= 0 && continuity == state.continuity) return; // duplicate packet
    discontinuity = state.continuity >= 0 && continuity != ((state.continuity + 1) & 0x0f);
    state.continuity = continuity;
  }
  const uint8_t *data = packet + payload;
  std::size_t length = packetSize - payload;

  if (cfg & cfgRaw) {
    if (ringWrite(descriptor, packet, packetSize, word(descriptor + 8), nullptr) && (cfg & cfgSignal)) raiseSlot(slot);
  } else if (!(adaptation & 1) || length == 0) {
    // adaptation field only
  } else if (cfg & cfgSection) {
    if (discontinuity) {
      state.sectionActive = false;
      state.section.clear();
    }
    if (unitStart) {
      std::size_t pointer = data[0];
      if (pointer + 1 > length) return;
      if (state.sectionActive) collectSections(slot, state, word1, descriptor, data + 1, pointer);
      state.section.clear();
      state.sectionActive = true;
      collectSections(slot, state, word1, descriptor, data + 1 + pointer, length - 1 - pointer);
    } else if (state.sectionActive) {
      collectSections(slot, state, word1, descriptor, data, length);
    }
  } else if (cfg & 0x0c) {
    avPayload((cfg >> 2) & 3, descriptor, data, length);
  } else if (cfg & cfgPartial) {
    std::vector<uint8_t> record{static_cast<uint8_t>((unitStart ? 0x40 : 0) | (discontinuity ? 0x80 : 0) | continuity),
                                static_cast<uint8_t>(length)};
    record.insert(record.end(), data, data + length);
    if (ringWrite(descriptor, record.data(), record.size(), word(descriptor + 8), nullptr)) raiseSlot(slot);
  } else if (cfg & cfgSignal) {
    if (unitStart) {
      finishPes(slot, state, descriptor, true);
      state.pesActive = length >= 3 && data[0] == 0 && data[1] == 0 && data[2] == 1;
    } else if (discontinuity) {
      state.pesActive = false;
      state.pes.clear();
    }
    if (state.pesActive) {
      state.pes.insert(state.pes.end(), data, data + length);
      if (state.pes.size() >= 6) {
        std::size_t declared = static_cast<std::size_t>(state.pes[4]) << 8 | state.pes[5];
        if (declared && state.pes.size() >= declared + 6) {
          state.pes.resize(declared + 6);
          finishPes(slot, state, descriptor, true);
        }
      }
    }
  }
  setWord(dmaSlotWord, (word(dmaSlotWord) & 0xffff0000U) | 0x3f0U);
}

int readRegister(void *, uint32_t address, int width, uint32_t *value) {
  int result = checkWordAccess(address, width);
  if (result) return result;
  uint32_t offset = address - registerBase;
  if (offset >= 0x20 && offset < 0x30) *value = 0; // IntAck: write only
  else if (offset >= 0x40 && offset < 0x50) {
    uint64_t base = stc27() / 300;
    *value = (offset & 4) ? static_cast<uint32_t>(base >> 32) & 1 : static_cast<uint32_t>(base);
  } else *value = registers[offset / 4];
  return 0;
}

int writeRegister(void *, uint32_t address, int width, uint32_t value) {
  int result = checkWordAccess(address, width);
  if (result) return result;
  uint32_t offset = address - registerBase;
  if (offset < 0x10) return 0; // IntStatus: read only (set by the TC)
  if (offset >= 0x20 && offset < 0x30) {
    registers[(offset - 0x20) / 4] &= ~value;
  } else if (offset == 0x50) {
    stcBase = static_cast<uint64_t>(registers[0x54 / 4] & 1) << 32 | value;
    stcLoadCycle = elapsedCpuCycles();
  } else {
    registers[offset / 4] = (offset >= 0x10 && offset < 0x20) ? value & 0xffff : value;
  }
  if (offset == 0x30 && (value & 2)) {
    for (SlotState &state : slots) state = SlotState{};
  }
  return 0;
}
} // namespace

MemoryDevice sti5512PtiRegisterDevice() {
  return {registerBase, registerEnd, nullptr, readRegister, writeRegister};
}

void resetSti5512Pti() {
  registers = {};
  stcBase = 0;
  stcLoadCycle = elapsedCpuCycles();
  inputPackets = matchedPackets = droppedUnits = 0;
  for (SlotState &state : slots) state = SlotState{};
}

bool sti5512PtiInterruptLine() {
  for (std::size_t i = 0; i < 4; ++i) {
    if (registers[i] & registers[4 + i] & 0xffff) return true;
  }
  return false;
}

void sti5512PtiInput(const uint8_t *packet) {
  uint32_t mode = registers[0x30 / 4];
  if (!(mode & 1) || (mode & 2) || !(word(iifFifoEnable) & 1)) return;
  ++inputPackets;
  processPacket(packet);
  publishStc();
}

uint64_t sti5512PtiInputPackets() {
  return inputPackets;
}

uint64_t sti5512PtiMatchedPackets() {
  return matchedPackets;
}

uint64_t sti5512PtiDroppedUnits() {
  return droppedUnits;
}
