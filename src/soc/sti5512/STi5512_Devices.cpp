#include "STi5512_Devices.h"

#include "../../core/cpu/st20.h"
#include "../../core/memory/memory.h"
#include "STi5512_AudioDecoder.h"
#include "STi5512_Display.h"
#include "STi5512_Pti.h"
#include "STi5512_PtiDma.h"
#include "STi5512_RegisterBlocks.h"
#include "STi5512_VideoDecoder.h"

#include <array>
#include <utility>

/*
 * STi5512 Register Manual (7145562 B): ASC (chapter 3),
 * INC (Table 13) and INL (Table 14, chapter 14). Interrupt numbers are from the
 * STi5512 datasheet (7145560 B) Table 50: SSC0 = 5, SSC1 = 6, ASC3 = 7, ASC2 = 8, ASC1 = 9,
 * ASC0 = 10, PWM and capture = 11, PTI = 14, video decoder = 21, audio decoder = 22.
 */
namespace {
constexpr uint32_t levelBase = 0x20011000U;
constexpr uint32_t levelSources = 23;
constexpr uint32_t ptiSource = 14;
constexpr uint32_t videoSource = 21;
constexpr uint32_t audioSource = 22;
Sti55xxPeripherals peripherals{false};
std::array<uint32_t, 0x80 / 4> levels{};
uint8_t videoRevision = 0;
bool initialized = false;

int readLevel(void *, uint32_t address, int width, uint32_t *value) {
  int result = checkWordAccess(address, width);
  if (result) return result;
  *value = levels[(address - levelBase) / 4];
  return 0;
}

int writeLevel(void *, uint32_t address, int width, uint32_t value) {
  int result = checkWordAccess(address, width);
  if (result) return result;
  uint32_t offset = address - levelBase;
  if (offset == 0x7c) return DEVICE_ACCESS_UNSUPPORTED;
  if (offset < levelSources * 4) levels[offset / 4] = value & 7U;
  else if (offset == 0x60 || offset == 0x64) levels[offset / 4] = value & 0xfU;
  else levels[offset / 4] = value;
  return 0;
}

/* 0x20000500: configuration word (board clock mode). The DSR9500 loader's asc_init
 * (Decompilation/DSR9500 src/m_ab80.c) picks ASC reload 0x20 for low six bits 1 and 0x1b
 * otherwise; the main application's STAPI GetClockInfo (a_7feb1494.c) maps bits 1:0 to 1 =
 * 60 MHz CPU, 1 MHz/15625 Hz timers, 2 = 39.9 MHz, 3 = 49.875 MHz, and fails for 0 (clock rates
 * then read 0, which breaks STAUD's A/V synchronisation pause). Read-only; 0 unless the board
 * sets it (the DSR9500 sets 1, matching the modelled 60 MHz CPU clock). */
constexpr uint32_t configWordAddress = 0x20000500U;
uint32_t configWord = 0;
/* any width: the DSR9500 main application reads this area with devlb */
int readConfigWord(void *, uint32_t, int, uint32_t *value) {
  *value = configWord;
  return 0;
}
int writeConfigWord(void *, uint32_t, int, uint32_t) {
  return 0; /* read-only: writes are ignored */
}
void acknowledgeInterrupt() {
  acknowledgeInc(peripherals.controller);
}

int registerDevices() {
  std::vector<MemoryDevice> devices = sti5512RegisterBlockDevices();
  devices.push_back(sti5512PtiDmaDevice());
  devices.push_back(sti5512PtiRegisterDevice());
  devices.push_back({levelBase, levelBase + 0x7f, nullptr, readLevel, writeLevel});
  devices.push_back({configWordAddress, configWordAddress + 3, nullptr, readConfigWord, writeConfigWord});
  return peripherals.registerDevices(std::move(devices));
}

int resetDevices() {
  resetSti5512RegisterBlocks(videoRevision);
  resetSti5512PtiDma();
  resetSti5512Pti();
  resetSti5512AudioDecoder();
  resetSti5512Display();
  resetSti5512VideoDecoder();
  peripherals.reset();
  levels = {};
  return 0;
}
}

int pollSti5512Devices() {
  if (!initialized) return 0;
  uint32_t inputs = peripherals.tickSerial();
  tickSti5512PtiDma();
  inputs |= peripherals.tickBoardAndPwm();
  if (tickSti5512Display()) inputs |= 1U << videoSource;
  tickSti5512AudioDecoder();
  if (sti5512AudioInterruptLine()) inputs |= 1U << audioSource;
  if (sti5512PtiInterruptLine()) inputs |= 1U << ptiSource;
  levels[0x7c / 4] = inputs;
  uint32_t routedLevels = 0;
  for (uint32_t source = 0; source < levelSources; ++source) {
    if (inputs & (1U << source)) routedLevels |= 1U << levels[source];
  }
  return pollInc(peripherals.controller, routedLevels);
}

int initSti5512Devices(uint8_t revision) {
  if (!initialized) {
    int result = registerDevices();
    if (result) return result;
  }
  videoRevision = revision;
  resetDevices();
  initialized = true;
  peripherals.attach(pollSti5512Devices, acknowledgeInterrupt, resetDevices);
  return 0;
}

void shutdownSti5512Devices() {
  if (!initialized) return;
  peripherals.detach();
  initialized = false;
}

void setSti5512ConfigWord(uint32_t value) {
  configWord = value;
}

Sti55xxPeripherals &sti5512Peripherals() {
  return peripherals;
}
