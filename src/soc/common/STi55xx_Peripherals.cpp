#include "STi55xx_Peripherals.h"
#include "../../core/cpu/st20.h"

#include <utility>

namespace {
constexpr uint32_t sdramStart = 0xc0000000U;
constexpr uint32_t sdramEnd = 0xc07fffffU;
constexpr uint32_t controllerBase = 0x20000000U;

/* the device numbered PORT, or null */
template <typename Devices> auto at(Devices &devices, int port) -> decltype(&devices[0]) {
  return port >= 0 && static_cast<std::size_t>(port) < devices.size() ? &devices[static_cast<std::size_t>(port)] : nullptr;
}
}

Sti55xxPeripherals::Sti55xxPeripherals(bool clearAll, std::vector<PioPort> extraPio)
    : pio{{0x2000c000U, 0}, {0x2000d000U, 1}, {0x2000e000U, 2}, {0x2000f000U, 3}, {0x20010000U, 4}},
      controller{controllerBase, clearAll} {
  pio.insert(pio.end(), extraPio.begin(), extraPio.end());
}

int Sti55xxPeripherals::registerDevices(std::vector<MemoryDevice> devices) {
  for (auto &device : asc) devices.push_back({device.base, device.base + ascRegisterEnd, &device, readAsc, writeAsc});
  for (auto &device : ssc) devices.push_back({device.base, device.base + sscRegisterEnd, &device, readSsc, writeSsc});
  devices.push_back({pwm.base, pwm.base + pwmRegisterEnd, &pwm, readPwm, writePwm});
  for (auto &port : pio) devices.push_back({port.base, port.base + pioRegisterEnd, &port, readPio, writePio});
  devices.push_back({controllerBase, controllerBase + incRegisterEnd, &controller, readInc, writeInc});
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

void Sti55xxPeripherals::unregisterDevices() {
  for (uint32_t base : registeredBases) unregisterMemoryDevice(base);
  registeredBases.clear();
}

void Sti55xxPeripherals::reset() {
  for (auto &device : asc) resetAsc(device);
  for (auto &device : ssc) resetSsc(device);
  resetPwm(pwm);
  for (auto &port : pio) resetPio(port);
  resetInc(controller);
}

uint32_t Sti55xxPeripherals::tickSerial() {
  uint32_t inputs = 0;
  for (auto &device : asc) {
    if (tickAsc(device)) inputs |= 1U << device.source;
  }
  for (auto &device : ssc) {
    if (tickSsc(device)) inputs |= 1U << device.source;
  }
  return inputs;
}

uint32_t Sti55xxPeripherals::tickBoardAndPwm() {
  if (boardPoller) boardPoller();
  return tickPwm(pwm) ? 1U << pwmSource : 0;
}

void Sti55xxPeripherals::attach(int (*poller)(), void (*acknowledge)(), int (*reboot)()) {
  for (auto &device : asc) device.transmitted.clear();
  /* unwritten shared SDRAM reads as zero: boot loaders (the DSR9500's among them) read a
   * warm-restart mailbox there before anything has written it */
  setZeroFillRange(sdramStart, sdramEnd);
  setPeripheralPoller(poller);
  setInterruptReturnObserver(acknowledge);
  setRebootObserver(reboot);
}

void Sti55xxPeripherals::detach() {
  setZeroFillRange(1, 0);
  unregisterDevices();
  setPeripheralPoller(nullptr);
  setInterruptReturnObserver(nullptr);
  setRebootObserver(nullptr);
  for (auto &device : asc) {
    device.transmitObserver = nullptr;
    device.received.clear();
  }
  for (auto &device : ssc) device.slaves.clear();
  setPioObserver(nullptr);
  setPioInputSource(nullptr);
  boardPoller = nullptr;
}

std::vector<uint8_t> Sti55xxPeripherals::takeAscTransmit(int port) {
  AscDevice *device = at(asc, port);
  return device ? std::exchange(device->transmitted, {}) : std::vector<uint8_t>{};
}

void Sti55xxPeripherals::setAscTransmitObserver(int port, void (*observer)(uint8_t byte)) {
  if (AscDevice *device = at(asc, port)) device->transmitObserver = observer;
}

void Sti55xxPeripherals::feedAscReceive(int port, const std::vector<uint8_t> &bytes) {
  if (AscDevice *device = at(asc, port)) ::feedAscReceive(*device, bytes);
}

int Sti55xxPeripherals::attachI2cDevice(int port, I2cRegisterDevice *device) {
  SscDevice *bus = at(ssc, port);
  if (bus == nullptr || device == nullptr) return INVALID_MEMORY_DEVICE;
  bus->slaves.push_back(device);
  return 0;
}

void Sti55xxPeripherals::i2cBusCondition(int port, bool start) {
  if (SscDevice *bus = at(ssc, port)) sscBusCondition(*bus, start);
}

void Sti55xxPeripherals::setPioObserver(PioOutputObserver observer) {
  for (auto &port : pio) port.observer = observer;
}

void Sti55xxPeripherals::setPioInputSource(PioInputSource source) {
  for (auto &port : pio) port.input = source;
}

uint8_t Sti55xxPeripherals::pioOutput(int port) const {
  const PioPort *found = at(pio, port);
  return found ? found->registers[0] : 0;
}

void Sti55xxPeripherals::pwmCaptureEdge(int input, bool rising) {
  ::pwmCaptureEdge(pwm, input, rising);
}
