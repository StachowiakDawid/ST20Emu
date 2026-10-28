#include "STi5518_Devices.h"
#include "../../core/cpu/omr.h"
#include "../../core/cpu/st20.h"
#include "../../core/memory/memory.h"
#include "STi5518_Display.h"
#include "STi5518_Irb.h"
#include "STi5518_RegisterBlocks.h"

#include <array>
#include <utility>

/*
 * STi5518 Register Manual (7170180A) and datasheet (7170179 D) Table 34 interrupt numbers:
 * PIO0-4 = 0-4, SSC0 = 5, SSC1 = 6, ASC3 = 7, ASC2 = 8, ASC1 = 9, ASC0 = 10, PWM = 11,
 * PIO5 = 17, IR blaster = 18, audio = 27, video = 28. The interrupt level controller (ILC) routes each of the 31 sources to a level
 * (0x00-0x78), latches the raw inputs at 0x7c, keeps a trigger mode per source at 0x100 and
 * an enable mask at 0x1bc with set/clear registers at 0x1b8/0x1b4.
 *
 * The low power module's watchdog (LPCBaseAddress 0x20000400): writing LPM_ALARMSTART while
 * LPM_WDENABLE is set starts the alarm counter, which counts LPM_ALARM ticks of the low
 * priority timer (15625 Hz) and then reboots the chip, setting LPM_WDFLAG. The low power
 * timer itself is plain storage.
 */
namespace {
constexpr uint32_t lowPowerBase = 0x20000400U;
constexpr uint32_t routingBase = 0x20011000U;
constexpr uint32_t sources = 31;
constexpr uint32_t irbSource = 18;
constexpr uint32_t audioSource = 27;
constexpr uint32_t videoSource = 28;
Sti55xxPeripherals peripherals{true, {{0x2000a100U, 5}}};
std::array<uint32_t, 0x1f0 / 4> routing{};
std::array<bool, sources> previousSources{};
std::array<uint32_t, 0x120 / 4> lowPower{};
bool watchdogRunning = false;
uint64_t watchdogStart = 0;
constexpr uint64_t cyclesPerLowPowerTick = CPU_CLOCK / 15625;
bool initialized = false;

int readRouting(void *, uint32_t address, int width, uint32_t *value) {
  int result = checkWordAccess(address, width);
  if (result) return result;
  *value = routing[(address - routingBase) / 4];
  return 0;
}

int writeRouting(void *, uint32_t address, int width, uint32_t value) {
  int result = checkWordAccess(address, width);
  if (result) return result;
  uint32_t offset = address - routingBase;
  if (offset == 0x1b8) routing[0x1bc / 4] |= value & 0x7fffffffU;
  else if (offset == 0x1b4) routing[0x1bc / 4] &= ~value;
  else routing[offset / 4] = offset < 0x7c || (offset >= 0x100 && offset < 0x17c) ? value & 7U : value;
  return 0;
}

int readLowPower(void *, uint32_t address, int width, uint32_t *value) {
  int result = checkWordAccess(address, width);
  if (result) return result;
  uint32_t offset = address - lowPowerBase;
  if (offset == 0x08 || offset == 0x18) return DEVICE_ACCESS_UNSUPPORTED;
  *value = lowPower[offset / 4];
  return 0;
}

int writeLowPower(void *, uint32_t address, int width, uint32_t value) {
  int result = checkWordAccess(address, width);
  if (result) return result;
  uint32_t offset = address - lowPowerBase;
  if (offset == 0x114) return DEVICE_ACCESS_UNSUPPORTED;
  lowPower[offset / 4] = value;
  if (offset == 0x00 || offset == 0x04) watchdogRunning = false;
  if (offset == 0x18 && (lowPower[0x110 / 4] & 1U)) {
    watchdogRunning = true;
    watchdogStart = elapsedCpuCycles();
  }
  return 0;
}

void acknowledgeInterrupt() {
  acknowledgeInc(peripherals.controller);
}

int registerDevices() {
  std::vector<MemoryDevice> devices = sti5518RegisterBlockDevices();
  devices.push_back(sti5518IrbDevice());
  devices.push_back({lowPowerBase, lowPowerBase + 0x11f, nullptr, readLowPower, writeLowPower});
  devices.push_back({routingBase, routingBase + 0x1ef, nullptr, readRouting, writeRouting});
  return peripherals.registerDevices(std::move(devices));
}

int resetDevices() {
  resetSti5518RegisterBlocks();
  resetSti5518Display();
  resetSti5518Irb();
  peripherals.reset();
  routing = {};
  routing[0x1bc / 4] = 0x7fffffffU;
  for (uint32_t source = 0; source < sources; ++source) routing[0x100 / 4 + source] = 1;
  previousSources = {};
  lowPower = {};
  watchdogRunning = false;
  return 0;
}
}

int pollSti5518Devices() {
  if (!initialized) return 0;
  if (watchdogRunning &&
      elapsedCpuCycles() - watchdogStart >= uint64_t{lowPower[0x10 / 4]} * cyclesPerLowPowerTick) {
    int result = rebootProcessor(); // resets the devices, then LPM_WDFLAG reports the cause
    lowPower[0x114 / 4] = 1;
    return result;
  }
  uint32_t inputs = peripherals.tickSerial();
  inputs |= peripherals.tickBoardAndPwm();
  if (sti5518IrbInterruptLine()) inputs |= 1U << irbSource;
  if (tickSti5518Display()) inputs |= 1U << videoSource;
  if (sti5518AudioInterruptLine()) inputs |= 1U << audioSource;
  routing[0x7c / 4] = inputs;
  uint32_t routedLevels = 0;
  for (uint32_t source = 0; source < sources; ++source) {
    bool input = (inputs & (1U << source)) != 0;
    bool enabled = (routing[0x1bc / 4] & (1U << source)) != 0;
    if (enabled && triggered(routing[0x100 / 4 + source], input, previousSources[source]))
      routedLevels |= 1U << routing[source];
    previousSources[source] = input;
  }
  return pollInc(peripherals.controller, routedLevels);
}

int initSti5518Devices() {
  if (!initialized) {
    int result = registerDevices();
    if (result) return result;
  }
  resetDevices();
  initialized = true;
  peripherals.attach(pollSti5518Devices, acknowledgeInterrupt, resetDevices);
  return 0;
}

void shutdownSti5518Devices() {
  if (!initialized) return;
  peripherals.detach();
  initialized = false;
}

Sti55xxPeripherals &sti5518Peripherals() {
  return peripherals;
}
