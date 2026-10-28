#include "Stb6300.h"
#include "AmdFlash.h"
#include "Board.h"

#include "../core/cpu/omr.h"
#include "../core/cpu/st20.h"
#include "../core/memory/memory.h"
#include "../soc/common/STi55xx_Ssc.h"
#include "../soc/sti5518/STi5518_Devices.h"
#include "../soc/sti5518/STi5518_Irb.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <format>
#include <utility>
#include <vector>

/*
 * Front-panel controller on ASC1. The boot loader (flash 0x7fff02cc, run from RAM 0xc0400000)
 * exchanges 4-byte packets with it: command, two data bytes and a checksum that makes the
 * bytes sum to 0 (0xc0401197). At start-up it sends 0x28 until a 4-byte reply starting with
 * 0x29 arrives (0xc0401038). Data bytes of 16-bit values are little-endian.
 *
 * The main application authenticates the controller (0x7fefa80c): it sends 0x24 with the
 * challenge c = K ^ T[s % N1] ^ T[N1 + s % N2], where s counts exchanges since the last 0x28
 * (starting at 1), K is the session key and T a table of 1024 words. The controller answers
 * 0x36 with r = K ^ T[x % N1] ^ T[N1 + x % N2], x = K + s (0x7fefa8d7); a wrong answer makes
 * the application reboot through the watchdog, and three unanswered challenges restart the
 * 0x28 handshake. T, N1 = 307, N2 = 691 and the first K are in flash at 0x7ff7b7fc.
 *
 * Keys are 0x10 packets with an active-low key bit. Standby is a watchdog reboot after which
 * the application powers up only if the controller reports the power key (0xef) right after
 * the 0x28 handshake (0x7fef8ba3; 0xf7 selects another start mode); otherwise it returns to
 * standby, so the power key is also remembered for the next handshake.
 *
 * The display packets carry active-low 7-segment digits (bit 7 low = dot): 0x41 the first two,
 * 0x43 the last two, 0x45 the LEDs.
 *
 * Flash: 2 MB x16 at 0x7fe00000. The loader's probe (0xc042ab30) accepts Intel, Sharp, MXIC,
 * Fujitsu, AMD and ST parts, the application's (RAM 0xc07394b0) only ST, MXIC, SST and Hynix
 * codes; it is modelled as an ST M29W160ET (manufacturer 0x20, device 0x22c4: AMD command
 * set, top boot block, which matches a loader in the top 64 KB).
 *
 * I2C: the driver (0x7feacf40) generates START and STOP on PIO1 (bit 0 SDA, bit 1 SCL) and lets
 * SSC0 shift the frames. The only chip it addresses is at 0xd0, the QPSK demodulator of the
 * tuner ("gx" driver), here a register file that acknowledges its address; until it answers,
 * the application retries and its user interface does not start.
 *
 * Remote control: the IR receiver feeds the IR blaster's RC receiver; the application decodes
 * NEC frames from the symbol periods (interrupt handler 0x7fe7abec) with customer code 0xfd01
 * (a second decoder, for PWM captures at 0x7fe7a7a6, expects 0xf708). The key task (0x7fe79e2d)
 * maps the commands to the application's key codes: 0xdc menu (0xd5); digits 0-9 are 0x86, 0x91,
 * 0x92, 0xcc, 0x8d, 0x8e, 0xc8, 0x89, 0x8a, 0xc4 (0x30-0x39); 0xc1, 0x99, 0xca, 0xd2 give
 * 0x9a-0x9d, the codes of the front-panel arrows; 0x94 gives 0xf7 and 0xc5 0xf9.
 */
namespace {
constexpr int frontPort = 1;
constexpr uint32_t keyTableAddress = 0x7ff7b7fcU;
bool active = false;
std::vector<uint8_t> packet;
std::array<std::array<uint8_t, 3>, 3> displayPackets{}; // last 0x41, 0x43, 0x45 packets
std::array<uint16_t, 1024> keyTable{};
uint32_t firstModulus = 1;
uint32_t secondModulus = 1;
uint16_t exchanges = 0;
constexpr uint8_t powerKey = 0xef;
bool powerPending = false;
constexpr uint32_t necCustomer = 0xfd01;
constexpr uint64_t cyclesPerMicrosecond = CPU_CLOCK / 1000000;
constexpr int i2cPioPort = 1;
constexpr uint8_t sdaBit = 0x01;
constexpr uint8_t sclBit = 0x02;
I2cRegisterDevice demodulator{0xd0};
uint8_t i2cLines = sdaBit | sclBit;

void observePio(int port, uint8_t out) {
  if (port != i2cPioPort) return;
  uint8_t lines = out & (sdaBit | sclBit);
  bool clockHigh = (lines & sclBit) && (i2cLines & sclBit);
  if (clockHigh && (i2cLines & sdaBit) && !(lines & sdaBit)) sti5518Peripherals().i2cBusCondition(0, true);
  if (clockHigh && !(i2cLines & sdaBit) && (lines & sdaBit)) sti5518Peripherals().i2cBusCondition(0, false);
  i2cLines = lines;
}

struct IrSymbol {
  uint64_t time; // when the receiver has measured it: the start of the next pulse
  uint32_t on, period;
  bool last;
};
std::deque<IrSymbol> irSymbols;

void pollBoard() {
  uint64_t now = elapsedCpuCycles();
  while (!irSymbols.empty() && now >= irSymbols.front().time) {
    const IrSymbol &symbol = irSymbols.front();
    sti5518IrbReceive(symbol.on, symbol.period, symbol.last);
    irSymbols.pop_front();
  }
}

void loadKeyTable() {
  for (uint32_t index = 0; index < keyTable.size(); ++index) {
    uint32_t value = 0;
    readBytes(keyTableAddress + index * 2, 2, &value);
    keyTable[index] = static_cast<uint16_t>(value);
  }
  readBytes(keyTableAddress + 0x800, 4, &firstModulus);
  readBytes(keyTableAddress + 0x804, 4, &secondModulus);
  if (firstModulus == 0 || secondModulus == 0 || firstModulus + secondModulus > keyTable.size())
    firstModulus = secondModulus = 1;
}

uint16_t keyMask(uint16_t index) {
  return keyTable[index % firstModulus] ^ keyTable[firstModulus + index % secondModulus];
}

/* character for an active-low segment byte (bit 0 = a ... bit 6 = g; bit 7 low = dot), using
 * the application's font (digits at flash 0x7ff3239c) and the usual letter shapes */
std::string segmentText(uint8_t segments) {
  static constexpr std::pair<uint8_t, char> font[] = {
      {0x40, '0'}, {0x79, '1'}, {0x24, '2'}, {0x30, '3'}, {0x19, '4'}, {0x12, '5'}, {0x02, '6'},
      {0x78, '7'}, {0x00, '8'}, {0x18, '9'}, {0x10, '9'}, {0x08, 'A'}, {0x03, 'b'}, {0x46, 'C'},
      {0x27, 'c'}, {0x21, 'd'}, {0x06, 'E'}, {0x0e, 'F'}, {0x42, 'G'}, {0x09, 'H'}, {0x0b, 'h'},
      {0x61, 'J'}, {0x47, 'L'}, {0x2b, 'n'}, {0x23, 'o'}, {0x0c, 'P'}, {0x2f, 'r'}, {0x07, 't'},
      {0x41, 'U'}, {0x63, 'u'}, {0x11, 'y'}, {0x3f, '-'}, {0x77, '_'}, {0x7f, ' '}};
  std::string text = "?";
  for (auto [pattern, character] : font) {
    if (pattern == (segments & 0x7fU)) text = std::string(1, character);
  }
  if (!(segments & 0x80U)) text += '.';
  return text;
}

void sendPacket(uint8_t command, uint8_t first, uint8_t second) {
  uint8_t checksum = static_cast<uint8_t>(0 - command - first - second);
  sti5518Peripherals().feedAscReceive(frontPort, {command, first, second, checksum});
}

void frontPanelReceive(uint8_t byte) {
  packet.push_back(byte);
  if (packet.size() < 4) return;
  uint8_t command = packet[0];
  uint16_t value = static_cast<uint16_t>(packet[1] | packet[2] << 8);
  packet.clear();
  if (command == 0x41 || command == 0x43 || command == 0x45)
    displayPackets[(command - 0x41U) / 2] = {command, static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)};
  if (command == 0x28) {
    exchanges = 0;
    sendPacket(0x29, 0, 0);
    if (std::exchange(powerPending, false)) sendPacket(0x10, powerKey, 0);
  } else if (command == 0x24) {
    if (keyTable[0] == 0 && keyTable[1] == 0) loadKeyTable();
    uint16_t sequence = ++exchanges;
    uint16_t key = value ^ keyMask(sequence);
    uint16_t answer = key ^ keyMask(static_cast<uint16_t>(key + sequence));
    sendPacket(0x36, static_cast<uint8_t>(answer), static_cast<uint8_t>(answer >> 8));
  }
}
}

int initStb6300Board() {
  packet.clear();
  keyTable = {};
  exchanges = 0;
  /* STB6300_POWER=1: the power key is held at power-up, so the receiver starts switched on */
  powerPending = std::getenv("STB6300_POWER") != nullptr;
  int result = initAmdFlash({0x7fe00000U, 0x200000U, 0x0020, 0x22c4});
  if (result) return result;
  sti5518Peripherals().setAscTransmitObserver(frontPort, frontPanelReceive);
  displayPackets = {};
  irSymbols.clear();
  demodulator.registers = {};
  demodulator.pointer = 0;
  demodulator.pointerWritten = false;
  i2cLines = sdaBit | sclBit;
  sti5518Peripherals().setPioObserver(observePio);
  sti5518Peripherals().boardPoller = pollBoard;
  result = sti5518Peripherals().attachI2cDevice(0, &demodulator);
  if (result) return result;
  active = true;
  setActiveBoard(Board::Stb6300);
  return 0;
}

bool stb6300Active() {
  return active;
}

std::string stb6300SendKey(std::string_view key) {
  unsigned code = 0;
  if (key == "power") code = powerKey;
  else {
    auto [end, error] = std::from_chars(key.data(), key.data() + key.size(), code, 16);
    if (error != std::errc{} || end != key.data() + key.size() || code > 0xff)
      return std::format("Invalid key: {} (expected power or a hex code byte)", key);
  }
  if (code == powerKey) powerPending = true;
  sendPacket(0x10, static_cast<uint8_t>(code), 0);
  return "";
}


std::string stb6300SendIr(std::string_view key) {
  unsigned command = 0;
  if (key == "menu") command = 0xdc;
  else {
    auto [end, error] = std::from_chars(key.data(), key.data() + key.size(), command, 16);
    if (error != std::errc{} || end != key.data() + key.size() || command > 0xff)
      return std::format("Invalid remote command: {} (expected menu or a hex byte)", key);
  }
  uint32_t frame = necCustomer | command << 16 | (~command & 0xffU) << 24;
  uint64_t time = std::max(elapsedCpuCycles(), irSymbols.empty() ? 0 : irSymbols.back().time) + 1000 * cyclesPerMicrosecond;
  auto pulse = [&time](uint32_t mark, uint32_t space, bool last) {
    time += uint64_t{mark + space} * cyclesPerMicrosecond;
    irSymbols.push_back({time, mark, mark + space, last});
  };
  pulse(9000, 4500, false);
  for (int bit = 0; bit < 32; ++bit) pulse(560, (frame >> bit) & 1 ? 1690 : 560, false);
  pulse(560, 40000, true);
  return "";
}

std::string stb6300Display() {
  std::string text;
  for (std::size_t index = 0; index < 2; ++index) {
    text += segmentText(displayPackets[index][1]);
    text += segmentText(displayPackets[index][2]);
  }
  std::string raw;
  for (const auto &display : displayPackets)
    raw += std::format("{}{:02x} {:02x} {:02x}", raw.empty() ? "" : ", ", display[0], display[1], display[2]);
  return std::format("\"{}\" ({})", text, raw);
}
