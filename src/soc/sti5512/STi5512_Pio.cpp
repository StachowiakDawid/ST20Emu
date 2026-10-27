#include "STi5512_Pio.h"

#include <array>

/*
 * STi5512 Register Manual (7145562 B), PIO chapter. Each register (Out, C0, C1, C2, Comp,
 * Mask) has Set and Clear aliases at +4 and +8; In is at 0x10. Pin levels come from the
 * board input source. Compare interrupts and the pin configuration modes are not modelled.
 */
namespace {
constexpr uint32_t pioBase = 0x2000c000U;
constexpr uint32_t pioStride = 0x1000U;
constexpr uint32_t pioEnd = 0x6b;
constexpr uint32_t inOffset = 0x10;

struct PioPort {
  int index;
  std::array<uint8_t, 7> registers{}; // Out, In (unused), C0, C1, C2, Comp, Mask
};

int checkAccess(uint32_t address, int width) {
  return width < 1 || width > 4 || (address & 3U) ? INVALID_DEVICE_ACCESS : 0;
}

std::array<PioPort, sti5512PioPorts> ports{{{0}, {1}, {2}, {3}, {4}}};
PioOutputObserver outputObserver = nullptr;
PioInputSource inputSource = nullptr;

int readPio(void *context, uint32_t address, int width, uint32_t *value) {
  int result = checkAccess(address, width);
  if (result) return result;
  auto &port = *static_cast<PioPort *>(context);
  uint32_t offset = address - (pioBase + static_cast<uint32_t>(port.index) * pioStride);
  uint8_t out = port.registers[0];
  if (offset == inOffset) *value = inputSource ? inputSource(port.index, out) : out;
  else if ((offset & 0xf) == 0) *value = port.registers[offset >> 4];
  else return DEVICE_ACCESS_UNSUPPORTED;
  return 0;
}

int writePio(void *context, uint32_t address, int width, uint32_t value) {
  int result = checkAccess(address, width);
  if (result) return result;
  auto &port = *static_cast<PioPort *>(context);
  uint32_t offset = address - (pioBase + static_cast<uint32_t>(port.index) * pioStride);
  if (offset == inOffset) return DEVICE_ACCESS_UNSUPPORTED;
  uint8_t &target = port.registers[offset >> 4];
  uint8_t bits = static_cast<uint8_t>(value);
  switch (offset & 0xf) {
    case 0: target = bits; break;
    case 4: target |= bits; break;
    case 8: target &= static_cast<uint8_t>(~bits); break;
    default: return DEVICE_ACCESS_UNSUPPORTED;
  }
  if ((offset >> 4) == 0 && outputObserver) outputObserver(port.index, port.registers[0]);
  return 0;
}
}

std::vector<MemoryDevice> sti5512PioDevices() {
  std::vector<MemoryDevice> devices;
  for (auto &port : ports) {
    uint32_t base = pioBase + static_cast<uint32_t>(port.index) * pioStride;
    devices.push_back({base, base + pioEnd, &port, readPio, writePio});
  }
  return devices;
}

void resetSti5512Pio() {
  for (auto &port : ports) port.registers = {};
}

void setSti5512PioObserver(PioOutputObserver observer) {
  outputObserver = observer;
}

void setSti5512PioInputSource(PioInputSource source) {
  inputSource = source;
}

uint8_t sti5512PioOutput(int port) {
  return port >= 0 && port < sti5512PioPorts ? ports[static_cast<std::size_t>(port)].registers[0] : 0;
}
