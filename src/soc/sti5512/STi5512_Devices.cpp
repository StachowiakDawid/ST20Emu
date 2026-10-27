#include "STi5512_Devices.h"

#include "../../core/cpu/st20.h"
#include "../../core/memory/memory.h"
#include "../common/STi55xx_Asc.h"
#include "../common/STi55xx_Inc.h"
#include "../common/STi55xx_Pwm.h"
#include "../common/STi55xx_Ssc.h"
#include "STi5512_Display.h"
#include "STi5512_PtiDma.h"
#include "STi5512_RegisterBlocks.h"
#include "STi5512_VideoDecoder.h"

#include <array>
#include <utility>

/*
 * STi5512 Register Manual (7145562 B): ASC (chapter 3),
 * INC (Table 13) and INL (Table 14, chapter 14). Interrupt numbers are from the
 * STi5512 datasheet (7145560 B) Table 50: SSC0 = 5, SSC1 = 6, ASC3 = 7, ASC2 = 8, ASC1 = 9,
 * ASC0 = 10, PWM and capture = 11, video decoder = 21.
 */
namespace {
constexpr uint32_t sdramStart = 0xc0000000U;
constexpr uint32_t sdramEnd = 0xc07fffffU;
constexpr uint32_t controllerBase = 0x20000000U;
constexpr uint32_t levelBase = 0x20011000U;
constexpr uint32_t levelSources = 23;
constexpr uint32_t pwmSource = 11;
constexpr uint32_t videoSource = 21;
BoardPoller boardPoller = nullptr;
std::array<AscDevice, 4> ascDevices{{{0x20003000U, 10}, {0x20004000U, 9}, {0x20005000U, 8}, {0x20006000U, 7}}};
std::array<SscDevice, 2> sscDevices{{{0x20009000U, 5}, {0x2000a000U, 6}}};
std::array<PioPort, sti5512PioPorts> pioPorts{{{0x2000c000U, 0}, {0x2000d000U, 1}, {0x2000e000U, 2},
    {0x2000f000U, 3}, {0x20010000U, 4}}};
PwmDevice pwmDevice{0x2000b000U};
InterruptController controller{controllerBase, false};
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

void acknowledgeInterrupt() {
  acknowledgeInc(controller);
}

std::vector<uint32_t> registeredBases;

void unregisterDevices() {
  for (uint32_t base : registeredBases) unregisterMemoryDevice(base);
  registeredBases.clear();
}

int registerDevices() {
  std::vector<MemoryDevice> devices = sti5512RegisterBlockDevices();
  for (auto &device : ascDevices) devices.push_back({device.base, device.base + ascRegisterEnd, &device, readAsc, writeAsc});
  for (auto &device : sscDevices) devices.push_back({device.base, device.base + sscRegisterEnd, &device, readSsc, writeSsc});
  devices.push_back(sti5512PtiDmaDevice());
  devices.push_back({pwmDevice.base, pwmDevice.base + pwmRegisterEnd, &pwmDevice, readPwm, writePwm});
  for (auto &port : pioPorts) devices.push_back({port.base, port.base + pioRegisterEnd, &port, readPio, writePio});
  devices.push_back({controllerBase, controllerBase + incRegisterEnd, &controller, readInc, writeInc});
  devices.push_back({levelBase, levelBase + 0x7f, nullptr, readLevel, writeLevel});
  for (const auto &device : devices) {
    int result = registerMemoryDevice(device);
    if (result) {
      unregisterDevices();
      return result;
    }
    registeredBases.push_back(device.start);
  }
  return 0;
}

int resetDevices() {
  resetSti5512RegisterBlocks(videoRevision);
  for (auto &device : ascDevices) resetAsc(device);
  for (auto &device : sscDevices) resetSsc(device);
  resetSti5512PtiDma();
  resetPwm(pwmDevice);
  for (auto &port : pioPorts) resetPio(port);
  resetSti5512Display();
  resetSti5512VideoDecoder();
  resetInc(controller);
  levels = {};
  return 0;
}
}

int pollSti5512Devices() {
  if (!initialized) return 0;
  uint32_t inputs = 0;
  for (auto &device : ascDevices) {
    if (tickAsc(device)) inputs |= 1U << device.source;
  }
  for (auto &device : sscDevices) {
    if (tickSsc(device)) inputs |= 1U << device.source;
  }
  tickSti5512PtiDma();
  if (boardPoller) boardPoller();
  if (tickPwm(pwmDevice)) inputs |= 1U << pwmSource;
  if (tickSti5512Display()) inputs |= 1U << videoSource;
  levels[0x7c / 4] = inputs;
  uint32_t routedLevels = 0;
  for (uint32_t source = 0; source < levelSources; ++source) {
    if (inputs & (1U << source)) routedLevels |= 1U << levels[source];
  }
  return pollInc(controller, routedLevels);
}

int initSti5512Devices(uint8_t revision) {
  if (!initialized) {
    int result = registerDevices();
    if (result) return result;
  }
  videoRevision = revision;
  resetDevices();
  for (auto &device : ascDevices) device.transmitted.clear();
  /* unwritten shared SDRAM reads as zero: boot loaders (the DSR9500's among them) read a
   * warm-restart mailbox there before anything has written it */
  setZeroFillRange(sdramStart, sdramEnd);
  initialized = true;
  setPeripheralPoller(pollSti5512Devices);
  setInterruptReturnObserver(acknowledgeInterrupt);
  setRebootObserver(resetDevices);
  return 0;
}

void shutdownSti5512Devices() {
  if (!initialized) return;
  setZeroFillRange(1, 0);
  unregisterDevices();
  setPeripheralPoller(nullptr);
  setInterruptReturnObserver(nullptr);
  setRebootObserver(nullptr);
  setSti5512PioObserver(nullptr);
  setSti5512PioInputSource(nullptr);
  boardPoller = nullptr;
  for (auto &device : sscDevices) device.slaves.clear();
  initialized = false;
}

std::vector<uint8_t> takeSti5512AscTransmit(int port) {
  if (port < 0 || port >= static_cast<int>(ascDevices.size())) return {};
  return std::exchange(ascDevices[static_cast<std::size_t>(port)].transmitted, {});
}

int attachSti5512I2cDevice(int port, I2cRegisterDevice *device) {
  if (port < 0 || port >= static_cast<int>(sscDevices.size()) || device == nullptr) return INVALID_MEMORY_DEVICE;
  sscDevices[static_cast<std::size_t>(port)].slaves.push_back(device);
  return 0;
}

void setSti5512BoardPoller(BoardPoller poller) {
  boardPoller = poller;
}

void setSti5512PioObserver(PioOutputObserver observer) {
  for (auto &port : pioPorts) port.observer = observer;
}

void setSti5512PioInputSource(PioInputSource source) {
  for (auto &port : pioPorts) port.input = source;
}

uint8_t sti5512PioOutput(int port) {
  return port >= 0 && port < sti5512PioPorts ? pioPorts[static_cast<std::size_t>(port)].registers[0] : 0;
}

void sti5512PwmCaptureEdge(int input, bool rising) {
  pwmCaptureEdge(pwmDevice, input, rising);
}
