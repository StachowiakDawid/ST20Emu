#include "STi55xx_Pio.h"
#include "../../core/memory/memory.h"

namespace {
constexpr uint32_t inOffset = 0x10;

int checkAccess(uint32_t address, int width) {
  return width < 1 || width > 4 || (address & 3U) ? INVALID_DEVICE_ACCESS : 0;
}
}

int readPio(void *context, uint32_t address, int width, uint32_t *value) {
  int result = checkAccess(address, width);
  if (result) return result;
  auto &port = *static_cast<PioPort *>(context);
  uint32_t offset = address - port.base;
  uint8_t out = port.registers[0];
  if (offset == inOffset) *value = port.input ? port.input(port.index, out) : out;
  else if ((offset & 0xf) == 0) *value = port.registers[offset >> 4];
  else return DEVICE_ACCESS_UNSUPPORTED;
  return 0;
}

int writePio(void *context, uint32_t address, int width, uint32_t value) {
  int result = checkAccess(address, width);
  if (result) return result;
  auto &port = *static_cast<PioPort *>(context);
  uint32_t offset = address - port.base;
  if (offset == inOffset) return DEVICE_ACCESS_UNSUPPORTED;
  uint8_t &target = port.registers[offset >> 4];
  uint8_t bits = static_cast<uint8_t>(value);
  switch (offset & 0xf) {
    case 0: target = bits; break;
    case 4: target |= bits; break;
    case 8: target &= static_cast<uint8_t>(~bits); break;
    default: return DEVICE_ACCESS_UNSUPPORTED;
  }
  if ((offset >> 4) == 0 && port.observer) port.observer(port.index, port.registers[0]);
  return 0;
}

void resetPio(PioPort &port) {
  port.registers = {};
}
