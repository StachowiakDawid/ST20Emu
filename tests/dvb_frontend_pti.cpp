/*
 * DSR9500 front end (HM1221 + tuner PLL + LNB latch), STi5512 PTI transport controller model,
 * PES parser and PTI DMA to the video decoder. The tables are set up the way ST's PTI driver
 * 3.6.0 (pti_set_buffer, pti_set_pid, pti_set_section_filter, ...) sets them up in the TC data
 * memory; transport packets come from a test transport stream "on air" through the front end.
 */
#include "../src/board/DSR9500.h"
#include "../src/board/Dsr9500Frontend.h"
#include "../src/core/cpu/omr.h"
#include "../src/core/cpu/st20.h"
#include "../src/core/memory/memory.h"
#include "../src/soc/common/STi55xx_Ssc.h"
#include "../src/soc/sti5512/STi5512_Devices.h"
#include "../src/soc/sti5512/STi5512_Pes.h"
#include "../src/soc/sti5512/STi5512_Pti.h"
#include "../src/soc/sti5512/STi5512_PtiDma.h"
#include "../src/soc/sti5512/STi5512_VideoDecoder.h"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <vector>

namespace {
int checks = 0;
int failures = 0;

void expect(const char *name, uint64_t actual, uint64_t expected) {
  ++checks;
  if (actual == expected) return;
  ++failures;
  std::printf("FAIL %s: got 0x%" PRIx64 ", expected 0x%" PRIx64 "\n", name, actual, expected);
}

/* I2C transactions as the firmware's i2c_qpsk_write/read do them */
void demodWrite(uint8_t reg, uint8_t value) {
  I2cRegisterDevice &chip = dsr9500QpskDevice();
  i2cSlaveBeginWrite(chip);
  i2cSlaveWrite(chip, reg);
  i2cSlaveWrite(chip, value);
}

uint8_t demodRead(uint8_t reg) {
  I2cRegisterDevice &chip = dsr9500QpskDevice();
  i2cSlaveBeginWrite(chip);
  i2cSlaveWrite(chip, reg);
  return i2cSlaveRead(chip);
}

void pllWord(uint8_t a, uint8_t b, uint8_t c) {
  demodWrite(0x6d, 0x40);
  demodWrite(0x72, a);
  demodWrite(0x72, b);
  demodWrite(0x72, c);
  demodWrite(0x6d, 0x42);
}

uint32_t word(uint32_t address) {
  uint32_t value = 0;
  readBytes(address, 4, &value);
  return value;
}

void setWord(uint32_t address, uint32_t value) {
  storeBytes(address, 4, value);
}

/* --- transport stream construction --- */
uint32_t crc32Mpeg(const std::vector<uint8_t> &data) {
  uint32_t crc = 0xffffffffU;
  for (uint8_t byte : data) {
    crc ^= static_cast<uint32_t>(byte) << 24;
    for (int bit = 0; bit < 8; ++bit) crc = (crc & 0x80000000U) ? (crc << 1) ^ 0x04c11db7U : crc << 1;
  }
  return crc;
}

std::vector<uint8_t> section(uint8_t tableId, uint16_t extension, const std::vector<uint8_t> &body) {
  std::vector<uint8_t> s{tableId, 0, 0, static_cast<uint8_t>(extension >> 8), static_cast<uint8_t>(extension), 0xc1, 0, 0};
  s.insert(s.end(), body.begin(), body.end());
  std::size_t length = s.size() - 3 + 4;
  s[1] = static_cast<uint8_t>(0xb0 | (length >> 8));
  s[2] = static_cast<uint8_t>(length);
  uint32_t crc = crc32Mpeg(s);
  for (int i = 3; i >= 0; --i) s.push_back(static_cast<uint8_t>(crc >> (8 * i)));
  return s;
}

/* packetises PAYLOAD on PID; sections get a pointer field */
void packetise(std::vector<uint8_t> &ts, uint16_t pid, const std::vector<uint8_t> &payload, bool isSection, int &cc,
               const uint8_t *pcr = nullptr) {
  std::size_t offset = 0;
  bool first = true;
  while (offset < payload.size() || first) {
    std::vector<uint8_t> packet{0x47, static_cast<uint8_t>((first ? 0x40 : 0) | (pid >> 8)), static_cast<uint8_t>(pid), 0};
    std::size_t room = 184;
    if (pcr && first) {
      packet[3] = static_cast<uint8_t>(0x30 | (cc & 15));
      packet.insert(packet.end(), {7, 0x10});
      packet.insert(packet.end(), pcr, pcr + 6);
      room -= 8;
    } else {
      packet[3] = static_cast<uint8_t>(0x10 | (cc & 15));
    }
    if (isSection && first) {
      packet.push_back(0);
      --room;
    }
    std::size_t take = std::min(room, payload.size() - offset);
    packet.insert(packet.end(), payload.begin() + static_cast<std::ptrdiff_t>(offset),
                  payload.begin() + static_cast<std::ptrdiff_t>(offset + take));
    offset += take;
    packet.resize(188, 0xff);
    ts.insert(ts.end(), packet.begin(), packet.end());
    cc = (cc + 1) & 15;
    first = false;
  }
}

constexpr uint32_t ptiBase = 0x20030000U;
constexpr uint32_t tc = 0x20038000U;
constexpr uint32_t sectionBuffer = 0xc0100000U;
constexpr uint32_t videoBuffer = 0xc0110000U;
constexpr uint32_t bufferSize = 0x1000;

void setPid(int slot, uint32_t pid) {
  uint32_t at = tc + static_cast<uint32_t>(slot >> 1) * 4;
  uint32_t pair = word(at);
  pair = (slot & 1) ? (pair & 0xffffU) | pid << 16 : (pair & 0xffff0000U) | pid;
  setWord(at, pair);
}

/* pti_set_buffer: slot descriptor, DMA descriptor DMA and (sections) state block REG */
void setBuffer(int slot, uint32_t cfg, int dma, uint32_t buffer, int reg) {
  uint32_t descriptor = tc + 0x360 + static_cast<uint32_t>(dma) * 20;
  setWord(descriptor, buffer);
  setWord(descriptor + 4, buffer + bufferSize - 16);
  setWord(descriptor + 8, buffer);
  setWord(descriptor + 12, buffer);
  setWord(descriptor + 16, buffer);
  uint32_t entry = tc + 0x60 + static_cast<uint32_t>(slot) * 16;
  setWord(entry, 0x20);
  setWord(entry + 4, reg >= 0 ? (0x8680U + static_cast<uint32_t>(reg) * 24) << 16 : 0xf0000000U);
  setWord(entry + 8, (descriptor - ptiBase) << 16 | 0xf000);
  setWord(entry + 12, cfg);
}

void runFor(uint64_t cycles) {
  for (uint64_t done = 0; done < cycles; done += 1000) {
    advanceTimers(1000);
    pollSti5512Devices();
  }
}
} // namespace

int main() {
  /* PES parser: MPEG-2 header with PTS, payload, unbounded packet, ES pass-through */
  {
    PesParser pes;
    std::vector<uint8_t> in{0x00, 0x00, 0x01, 0xb3, 0x12};                            // ES before any PES
    in.insert(in.end(), {0, 0, 1, 0xe0, 0, 0, 0x80, 0x80, 5, 0x21, 0x00, 0x01, 0x00, 0x01}); // PTS 0
    in.insert(in.end(), {0, 0, 1, 0x00, 0x55, 0, 0, 1, 0xb8});                         // pictures
    in.insert(in.end(), {0, 0, 1, 0xc0, 0, 5, 0x80, 0x00, 0, 0xff, 0xfd});            // bounded, no PTS
    std::vector<uint8_t> out;
    bool sawPts = false;
    for (uint8_t byte : in) {
      uint8_t bytes[4];
      std::size_t count = pes.feed(byte, bytes);
      uint64_t pts = 0;
      if (pes.takePts(&pts)) sawPts = pts == 0;
      out.insert(out.end(), bytes, bytes + count);
    }
    std::vector<uint8_t> expected{0, 0, 1, 0xb3, 0x12, 0, 0, 1, 0x00, 0x55, 0, 0, 1, 0xb8, 0xff, 0xfd};
    expect("PES headers stripped", out == expected, 1);
    expect("PES PTS reported", sawPts, 1);
    expect("PES mode detected", pes.inPesMode(), 1);
  }

  expect("memory init", memoryInit() != 0, 0);
  st20Init();
  expect("STi5512 devices", initSti5512Devices() != 0, 0);
  expect("DSR9500 board", initDsr9500Board() != 0, 0);

  /* a transport stream: PAT (program 1 -> PMT 0x1000), PMT (video 0x100), a video PES with PCR */
  std::vector<uint8_t> ts;
  int ccPat = 0, ccPmt = 0, ccVideo = 0;
  std::vector<uint8_t> pat = section(0x00, 1, {0x00, 0x01, 0xf0, 0x00});
  std::vector<uint8_t> pmt = section(0x02, 1, {0xe1, 0x00, 0xf0, 0x00, 0x02, 0xe1, 0x00, 0xf0, 0x00});
  std::vector<uint8_t> pes{0, 0, 1, 0xe0, 0, 0, 0x80, 0x80, 5, 0x21, 0x00, 0x01, 0x00, 0x01};
  for (int b : {0x00, 0x00, 0x01, 0xb3, 0x2d, 0x02, 0x40, 0x33, 0xff, 0xff, 0xe0, 0x18}) pes.push_back(static_cast<uint8_t>(b));
  pes.resize(400, 0x00);
  const uint8_t pcr[6]{0x00, 0x00, 0x2a, 0x30, 0x7e, 0x00};
  packetise(ts, 0x0000, pat, true, ccPat);
  packetise(ts, 0x1000, pmt, true, ccPmt);
  packetise(ts, 0x0100, pes, false, ccVideo, pcr);
  for (int i = 0; i < 20; ++i) packetise(ts, 0x1fff, std::vector<uint8_t>(184, 0xff), false, ccVideo); // padding
  char path[] = "/tmp/st20emu-dvb-XXXXXX";
  int fd = mkstemp(path);
  expect("temporary TS", fd >= 0, 1);
  expect("write TS", write(fd, ts.data(), ts.size()) == static_cast<ssize_t>(ts.size()), 1);
  close(fd);
  std::string tsPath = path;

  expect("bad transponder rejected", dsr9500AddTransponder(tsPath + ":11000:27500").empty(), 0);
  expect("transponder outside LNB band rejected", dsr9500AddTransponder(tsPath + ":12900:27500:H").empty(), 0);
  expect("transponder accepted", dsr9500AddTransponder(tsPath + ":11000:27500:V").empty(), 1);

  /* HM1221 identity and reset release */
  expect("demod identity", demodRead(0), 0x5a);
  storeBytes(0x50400000U, 1, 0x10);
  storeBytes(0x50400000U, 1, 0x14); // reset released: registers cleared
  expect("demod identity after reset", demodRead(0), 0x5a);
  expect("tuner port ready", demodRead(0x6d) & 0xc0, 0x40);

  /* tune as the firmware does: 11000 MHz V, low band (IF 1250 MHz), 27500 kSym/s, FEC auto */
  storeBytes(0x50400000U, 1, 0x80 | 0x08 | 0x04); // LNB on, 13 V, tone off, demod running
  uint32_t x = static_cast<uint32_t>(27500 * 553.101111 + 0.5);
  demodWrite(1, static_cast<uint8_t>(x >> 16));
  demodWrite(2, static_cast<uint8_t>(x >> 8));
  demodWrite(3, static_cast<uint8_t>(x));
  demodWrite(7, 1);
  demodWrite(0x18, 0xae);
  pllWord(0x00, 0x10, 0x08);                                           // band word, full step
  pllWord(0xf8 | 4, static_cast<uint8_t>(1236 >> 5), static_cast<uint8_t>((1236 & 31) << 3)); // N = 1236
  expect("PLL locked", demodRead(0x66) & 0x20, 0x20);
  expect("carrier and FEC lock", demodRead(8) & 0x60, 0x60);
  expect("code rate 3/4", demodRead(0x22) & 0x0f, 3);
  expect("tracking settled", demodRead(0x55) & 0x18, 0x18);
  /* PLL 1236 * 1011.1 kHz = 1249719.6 kHz, 280.4 kHz below the IF: N = -280.4 * 16384 / 27500 */
  int offset = static_cast<int16_t>(static_cast<uint16_t>(demodRead(0x47) << 8 | (demodRead(0x48) & 0x0f) << 4)) >> 4;
  expect("carrier offset", static_cast<uint64_t>(offset + 1000), static_cast<uint64_t>(-167 + 1000));
  demodWrite(0x6a, 0x84);
  expect("level measurement done", demodRead(0x6a) & 0x80, 0);
  expect("signal level word", static_cast<uint64_t>(demodRead(0x68) << 8 | demodRead(0x69)), 11000);
  expect("quality register", demodRead(0x27), 0xc4);
  expect("BER ready", demodRead(0x10) & 0x40, 0x40);
  expect("lock reported to tests", dsr9500FrontendLocked(), 1);

  storeBytes(0x50400000U, 1, 0x80 | 0x20 | 0x08 | 0x04); // 18 V: horizontal, no transponder there
  expect("no lock on the other polarisation", demodRead(8) & 0x60, 0);
  storeBytes(0x50400000U, 1, 0x80 | 0x04); // tone on: high band LO 10600, nothing at IF 1250
  expect("no lock in the high band", demodRead(8) & 0x60, 0);
  storeBytes(0x50400000U, 1, 0x08 | 0x04); // LNB supply off
  expect("no lock without LNB power", demodRead(8) & 0x60, 0);
  storeBytes(0x50400000U, 1, 0x80 | 0x08 | 0x04);
  demodWrite(0x18, 0x20); // 5/6 only
  expect("no FEC lock at another code rate", demodRead(8) & 0x60, 0x20);
  demodWrite(0x18, 0xae);
  demodWrite(1, static_cast<uint8_t>((x >> 16) + 4)); // about 27974 kSym/s, 1.7 % off
  expect("no lock at another symbol rate", demodRead(8) & 0x60, 0);
  demodWrite(1, static_cast<uint8_t>(x >> 16));
  expect("lock again", demodRead(8) & 0x60, 0x60);

  /* pti_tc_init and the set-up of a PAT section slot (46), a PMT slot (45) and the video slot */
  for (uint32_t i = 0; i < 0x300; ++i) setWord(tc + i * 4, 0);
  for (int i = 0; i < 24; ++i) setWord(tc + static_cast<uint32_t>(i) * 4, 0xe000e000U);
  setWord(0x20032008U, 1);           // IIFFIFOEnable
  setWord(ptiBase + 0x30, 1);        // TCMode: run
  setWord(tc + 0xa40, 0xe000);       // no PCR PID
  setBuffer(46, 0x2010, 0, sectionBuffer, 0);
  setBuffer(45, 0x2010, 1, sectionBuffer + 0x2000, 1);
  setWord(tc + 0x680, 1U << 0);      // slot 46 uses filter 0
  setWord(tc + 0x680 + 24, 1U << 1); // slot 45 uses filter 1
  /* filter 0: table_id 0; filter 1: table_id 2, program 1 (section bytes 3-4 = filter bytes 1-2) */
  setWord(0x20034000U, 0);
  setWord(0x20034004U, 0);
  setWord(0x20034008U, 0x00000000U);
  setWord(0x2003400cU, 0xff000000U);
  setWord(0x20034010U, 0);
  setWord(0x20034014U, 0);
  setWord(0x20034018U, 0x02000100U);
  setWord(0x2003401cU, 0xffffff00U);
  setWord(tc + 0xa6c, 3);                    // both filters enabled
  setWord(ptiBase + 0x1c, 1U << (46 % 16)); // IntEnable3: slot 46
  setPid(46, 0x0000);
  setPid(45, 0x1000);
  /* video: slot 0, ring shared with PTI DMA channel 1 (pti_set_buffer stream_type_video) */
  setBuffer(0, 0x404, 2, videoBuffer, -1);
  setWord(ptiBase + 0x1004, videoBuffer);
  setWord(ptiBase + 0x1014, videoBuffer + bufferSize - 16);
  setWord(ptiBase + 0x1024, videoBuffer);
  setWord(ptiBase + 0x1034, videoBuffer);
  setWord(ptiBase + 0x1050, 0xf);
  setPid(0, 0x0100);
  setWord(tc + 0xa40, 0x0100); // PCR on the video PID
  setWord(ptiBase + 0x10, 0x100); // IntEnable0: PCR

  /* a little over one pass of the stream at 27500 kSym/s (about 25 packets per ms) */
  runFor(CPU_CLOCK / 1000);
  runFor(CPU_CLOCK / 1000);
  expect("packets delivered while locked", dsr9500FrontendPackets() >= 24, 1);
  expect("PTI accepted them", sti5512PtiInputPackets() == dsr9500FrontendPackets(), 1);

  uint32_t patDescriptor = tc + 0x360;
  uint32_t patWrite = word(patDescriptor + 12);
  expect("PAT sections written", (patWrite - sectionBuffer) % pat.size(), 0);
  expect("at least one PAT section", patWrite > sectionBuffer, 1);
  bool patMatches = true;
  for (std::size_t i = 0; i < pat.size(); ++i) {
    uint32_t byte = 0;
    readBytes(sectionBuffer + static_cast<uint32_t>(i), 1, &byte);
    patMatches = patMatches && byte == pat[i];
  }
  expect("PAT section bytes", patMatches, 1);
  expect("PAT slot interrupt status", word(ptiBase + 0x0c) & (1U << 14), 1U << 14);
  expect("PTI interrupt line", sti5512PtiInterruptLine(), 1);
  setWord(ptiBase + 0x2c, 1U << 14);
  expect("IntAck clears", word(ptiBase + 0x0c) & (1U << 14), 0);
  uint32_t pmtWrite = word(tc + 0x360 + 20 + 12);
  expect("PMT section written", pmtWrite - (sectionBuffer + 0x2000) >= pmt.size(), 1);
  uint32_t tableId = 0xff;
  readBytes(sectionBuffer + 0x2000, 1, &tableId);
  expect("PMT table id", tableId, 2);
  expect("PMT slot interrupt pending but masked", word(ptiBase + 0x0c) & (1U << 13), 1U << 13);

  /* filter 1 asks for program 2: no more PMT sections */
  setWord(0x20034018U, 0x02000200U);
  setWord(tc + 0x360 + 20 + 8, word(tc + 0x360 + 20 + 12)); // reader caught up
  uint32_t pmtBefore = word(tc + 0x360 + 20 + 12);
  runFor(CPU_CLOCK / 500);
  expect("filtered PMT not delivered", word(tc + 0x360 + 20 + 12), pmtBefore);

  /* video PES -> ring -> DMA channel 1 -> PES parser -> bit buffer */
  expect("video ring written", word(tc + 0x360 + 40 + 12) != videoBuffer, 1);
  expect("DMA1 write pointer follows", sti5512PtiDmaRegister(0x24), word(tc + 0x360 + 40 + 12));
  expect("DMA1 drained into the decoder", sti5512PtiDmaRegister(0x34), sti5512PtiDmaRegister(0x24));
  expect("video decoder parsed PES", sti5512VideoStatus().find("(PES)") != std::string::npos, 1);
  expect("PCR interrupt", word(ptiBase + 0x00) & 0x100, 0x100);
  expect("PCR captured", word(tc + 0xa44), 0x0000U | 0x2a30U << 16);

  /* the TC stops with TCMode 0 */
  setWord(ptiBase + 0x30, 0);
  uint64_t accepted = sti5512PtiInputPackets();
  runFor(CPU_CLOCK / 500);
  expect("stopped TC accepts nothing", sti5512PtiInputPackets(), accepted);

  /* losing lock stops the stream */
  storeBytes(0x50400000U, 1, 0x80 | 0x20 | 0x08 | 0x04);
  uint64_t delivered = dsr9500FrontendPackets();
  runFor(CPU_CLOCK / 500);
  expect("no packets without lock", dsr9500FrontendPackets(), delivered);

  std::remove(path);
  std::printf("DVB front end / PTI tests: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
