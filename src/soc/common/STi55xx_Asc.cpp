#include "STi55xx_Asc.h"
#include "../../core/memory/memory.h"

int checkWordAccess(uint32_t address, int width) {
  return width != 4 || (address & 3U) ? INVALID_DEVICE_ACCESS : 0;
}

int readAsc(void *context, uint32_t address, int width, uint32_t *value) {
  int result = checkWordAccess(address, width);
  if (result) return result;
  auto &device = *static_cast<AscDevice *>(context);
  auto &asc = device.registers;
  uint32_t offset = address - device.base;
  if (offset == 4 || offset == 0x20 || offset == 0x24) return DEVICE_ACCESS_UNSUPPORTED;
  *value = asc[offset / 4];
  if (offset == 8) asc[0x14 / 4] &= ~0x21U;
  return 0;
}

int writeAsc(void *context, uint32_t address, int width, uint32_t value) {
  int result = checkWordAccess(address, width);
  if (result) return result;
  auto &device = *static_cast<AscDevice *>(context);
  auto &asc = device.registers;
  uint32_t offset = address - device.base;
  if (offset == 8 || offset == 0x14) return DEVICE_ACCESS_UNSUPPORTED;
  if (offset == 4) {
    asc[1] = value & 0x1ffU;
    if (asc[3] & 0x80U) {
      if (device.transmitObserver) device.transmitObserver(static_cast<uint8_t>(value));
      else device.transmitted.push_back(static_cast<uint8_t>(value));
      device.transmitDelay = 2;
      asc[5] &= ~6U;
    }
  } else if (offset == 0x20) {
    device.transmitDelay = 0;
    asc[5] |= 6U;
  } else if (offset == 0x24) {
    asc[2] = 0;
    asc[5] &= ~0x21U;
  } else {
    uint32_t mask = offset == 0 ? 0xffffU : offset == 0xc ? 0x1fffU : offset == 0x10 ? 0x1ffU : 0xffU;
    asc[offset / 4] = value & mask;
  }
  return 0;
}

void resetAsc(AscDevice &device) {
  device.registers = {};
  device.registers[5] = 3;
  device.registers[10] = 1;
  device.previousSource = false;
  device.transmitDelay = 0;
  device.receiveDelay = 0;
}

void feedAscReceive(AscDevice &device, const std::vector<uint8_t> &bytes) {
  device.received.insert(device.received.end(), bytes.begin(), bytes.end());
}

bool tickAsc(AscDevice &device) {
  auto &asc = device.registers;
  if (device.transmitDelay && --device.transmitDelay == 0) asc[5] |= 6U;
  if (device.transmitDelay == 0 && (asc[3] & 0x80U)) asc[5] |= 6U;
  /* RxEnable and Run: the next byte arrives a few polls after RxBuffer was read */
  if ((asc[3] & 0x180U) == 0x180U && !(asc[5] & 1U) && !device.received.empty() &&
      ++device.receiveDelay >= 4) {
    device.receiveDelay = 0;
    asc[2] = device.received.front();
    device.received.pop_front();
    asc[5] |= 1U;
  }
  return (asc[3] & 0x80U) && (asc[5] & asc[4]);
}
