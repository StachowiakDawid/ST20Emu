#include "DSR9500.h"
#include "Board.h"

#include "../common/compat.h"
#include "../core/cpu/omr.h"
#include "../core/cpu/st20.h"
#include "../soc/common/STi55xx_Ssc.h"
#include "../soc/sti5512/STi5512_Devices.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <deque>
#include <format>
#include <initializer_list>
#include <utility>

/*
 * The firmware's STI2C setup (main application 0x7fe763ff) puts the tuner bus "back" on
 * SSC1 and opens the QPSK demodulator at bus address 0xaa (open parameters at ROM
 * 0x7ff2acd8). qpsk_reset (0x7fe81f3f) accepts the chip when register 0 reads 0x5a.
 * The bus "front" on SSC0 carries the CI controller (0x80, ROM 0x7ff2ace4) and the RF
 * modulator (0xca, ROM 0x7ff2acf0). All three chips are plain register files that acknowledge
 * their address: there is no tuner, signal, lock, CI module or RF output model.
 *
 * Front panel (front-panel module 0x7fe8f738): a 16-bit shift register clocked from PIO4
 * (data bit 3, clock bit 4) and latched by PIO1 bit 7. Bits 12-15 select one of the four
 * LED digits, bits 0-7 are its active-low segments. The same outputs drive the key matrix
 * columns; a pressed key connects its column to the return line PIO4 bit 2 (pulled high).
 *
 * Remote control: the IR receiver output (active low) drives PIO3 bit 3 and PWM CaptureIn0.
 * The firmware decodes NEC frames by the time between falling edges and accepts the
 * customer code 0xfe01; the UI maps commands 0-31 through its key table (0x7fe430a8). Key
 * names follow the UI's handling: in standby (ui_task_main) only command 10 and front-panel
 * column 0x80 (UI keys 0x1c, 0x40) wake the receiver, so both are named power.
 */
namespace {
constexpr int frontBus = 0;
constexpr int tunerBus = 1;
constexpr uint8_t qpskIdentity = 0x5a;
I2cRegisterDevice qpskDemodulator{0xaa};
I2cRegisterDevice ciController{0x80};
I2cRegisterDevice rfModulator{0xca};

constexpr int latchPort = 1;
constexpr uint8_t latchBit = 0x80;
constexpr int panelPort = 4;
constexpr uint8_t dataBit = 0x08;
constexpr uint8_t clockBit = 0x10;
constexpr uint8_t keyReturnBit = 0x04;
constexpr int irPort = 3;
constexpr uint8_t irBit = 0x08;
constexpr uint64_t cyclesPerMicrosecond = CPU_CLOCK / 1000000;
constexpr uint64_t keyHoldCycles = CPU_CLOCK / 1000 * 150;
constexpr uint32_t necCustomer = 0xfe01;

/* ASCII 0x20-0x7f -> segments, the firmware's table at RAM 0x92a4d */
constexpr std::array<uint8_t, 96> segmentTable{
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xbf, 0xff, 0xff,
    0xc0, 0xf9, 0xa4, 0xb0, 0x99, 0x92, 0x82, 0xf8, 0x80, 0x90, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0x88, 0xff, 0xff, 0xff, 0x86, 0x8e, 0x82, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc8, 0xab, 0xff,
    0x8c, 0xff, 0xff, 0xff, 0xff, 0xe3, 0xc1, 0xff, 0xff, 0xff, 0xff, 0xc6, 0xff, 0xf0, 0xff, 0xf7,
};

constexpr std::pair<std::string_view, int> irKeys[] = {
    {"0", 0}, {"1", 1}, {"2", 2}, {"3", 3}, {"4", 4}, {"5", 5}, {"6", 6}, {"7", 7}, {"8", 8}, {"9", 9},
    {"left", 13}, {"right", 14}, {"pgup", 18}, {"pgdn", 19}, {"exit", 24}, {"ok", 27},
    {"menu", 28}, {"down", 30}, {"up", 31}, {"power", 10}};

constexpr std::pair<std::string_view, int> panelKeys[] = {{"up", 0x04}, {"down", 0x08}, {"power", 0x80}};

std::array<uint8_t, sti5512PioPorts> previousOut{};
uint16_t shiftRegister = 0;
uint16_t latched = 0;
std::array<uint8_t, 4> digits{0xff, 0xff, 0xff, 0xff};
std::string shownText = "    ";
uint8_t heldColumn = 0;
uint64_t keyRelease = 0;
bool irLevel = true;
std::deque<std::pair<uint64_t, bool>> irEdges;
bool active = false;

void resetChip(I2cRegisterDevice &chip) {
  chip.registers = {};
  chip.pointer = 0;
  chip.pointerWritten = false;
}

char segmentChar(uint8_t segments) {
  segments |= 0x80; // the decimal point
  if (segments == 0xff) return ' ';
  for (std::size_t i = 0; i < segmentTable.size(); ++i) {
    if (segmentTable[i] == segments) return static_cast<char>(0x20 + i);
  }
  return '?';
}

std::string displayText() {
  std::string text;
  for (uint8_t segments : digits) text.push_back(segmentChar(segments));
  return text;
}

void latchOutputs() {
  latched = shiftRegister;
  for (int digit = 0; digit < 4; ++digit) {
    if (latched & (0x1000 << digit)) digits[static_cast<std::size_t>(digit)] = static_cast<uint8_t>(latched);
  }
  std::string text = displayText();
  if (text != shownText) {
    shownText = text;
    compat::println("LED> {}", text);
  }
}

void observeOutput(int port, uint8_t out) {
  uint8_t rising = static_cast<uint8_t>(out & ~previousOut[static_cast<std::size_t>(port)]);
  previousOut[static_cast<std::size_t>(port)] = out;
  if (port == panelPort && (rising & clockBit))
    shiftRegister = static_cast<uint16_t>(shiftRegister << 1 | ((out & dataBit) ? 1 : 0));
  if (port == latchPort && (rising & latchBit)) latchOutputs();
}

uint8_t inputLevels(int port, uint8_t out) {
  if (port == panelPort) {
    bool line = heldColumn ? (latched & heldColumn) != 0 : true;
    return static_cast<uint8_t>((out & ~keyReturnBit) | (line ? keyReturnBit : 0));
  }
  if (port == irPort) return static_cast<uint8_t>((out & ~irBit) | (irLevel ? irBit : 0));
  return out;
}

void pollBoard() {
  uint64_t now = elapsedCpuCycles();
  if (heldColumn && now >= keyRelease) heldColumn = 0;
  while (!irEdges.empty() && now >= irEdges.front().first) {
    bool level = irEdges.front().second;
    irEdges.pop_front();
    if (level == irLevel) continue;
    irLevel = level;
    sti5512Peripherals().pwmCaptureEdge(0, level);
  }
}

/* parses a decimal or 0x-prefixed number in [0, limit] */
bool parseNumber(std::string_view text, int limit, int *value) {
  int base = 10;
  if (text.starts_with("0x") || text.starts_with("0X")) {
    text.remove_prefix(2);
    base = 16;
  }
  auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), *value, base);
  return error == std::errc{} && end == text.data() + text.size() && *value >= 0 && *value <= limit;
}

template <std::size_t N>
bool lookup(const std::pair<std::string_view, int> (&table)[N], std::string_view key, int *value) {
  for (const auto &[name, number] : table) {
    if (name == key) {
      *value = number;
      return true;
    }
  }
  return false;
}
}

int initDsr9500Board() {
  for (I2cRegisterDevice *chip : {&qpskDemodulator, &ciController, &rfModulator}) resetChip(*chip);
  qpskDemodulator.registers[0] = qpskIdentity;
  previousOut = {};
  shiftRegister = latched = 0;
  digits = {0xff, 0xff, 0xff, 0xff};
  shownText = "    ";
  heldColumn = 0;
  irLevel = true;
  irEdges.clear();
  sti5512Peripherals().setPioObserver(observeOutput);
  sti5512Peripherals().setPioInputSource(inputLevels);
  sti5512Peripherals().boardPoller = pollBoard;
  active = true;
  setActiveBoard(Board::Dsr9500);
  int result = sti5512Peripherals().attachI2cDevice(tunerBus, &qpskDemodulator);
  if (!result) result = sti5512Peripherals().attachI2cDevice(frontBus, &ciController);
  if (!result) result = sti5512Peripherals().attachI2cDevice(frontBus, &rfModulator);
  return result;
}

std::string dsr9500SendIr(std::string_view key) {
  if (!active) return "remote control needs --board dsr9500";
  int command = 0;
  if (!lookup(irKeys, key, &command) && !parseNumber(key, 0xff, &command))
    return "unknown remote key: " + std::string(key);
  uint32_t frame = necCustomer | static_cast<uint32_t>(command) << 16 | static_cast<uint32_t>(~command & 0xff) << 24;
  uint64_t time = std::max(elapsedCpuCycles(), irEdges.empty() ? 0 : irEdges.back().first) + 1000 * cyclesPerMicrosecond;
  auto pulse = [&time](uint64_t mark, uint64_t space) {
    irEdges.emplace_back(time, false);
    time += mark * cyclesPerMicrosecond;
    irEdges.emplace_back(time, true);
    time += space * cyclesPerMicrosecond;
  };
  pulse(9000, 4500);
  for (int bit = 0; bit < 32; ++bit) pulse(560, (frame >> bit) & 1 ? 1690 : 560);
  pulse(560, 0);
  return {};
}

std::string dsr9500PressFrontPanel(std::string_view key) {
  if (!active) return "front panel needs --board dsr9500";
  int column = 0;
  if (!lookup(panelKeys, key, &column) && !parseNumber(key, 0xff, &column))
    return "unknown front-panel key: " + std::string(key);
  if (column == 0 || (column & (column - 1)))
    return "front-panel key must be one matrix column bit: " + std::string(key);
  heldColumn = static_cast<uint8_t>(column);
  keyRelease = elapsedCpuCycles() + keyHoldCycles;
  return {};
}

std::string dsr9500Display() {
  return displayText();
}

std::string dsr9500DisplaySegments() {
  std::string text;
  for (uint8_t segments : digits) text += std::format("{:02x} ", segments);
  text.pop_back();
  return text;
}
