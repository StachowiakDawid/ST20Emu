#include "STi5512_Devices.h"

#include "../../core/memory/memory.h"

namespace {
constexpr uint32_t revisionAddress = 0x78;
uint8_t videoRevision = 0;
bool initialized = false;

int readRevision(void *, uint32_t address, int width, uint32_t *value) {
  if (address != revisionAddress || width != 1) return INVALID_DEVICE_ACCESS;
  *value = videoRevision;
  return 0;
}
}

int initSti5512Devices(uint8_t revision) {
  if (!initialized) {
    int result = registerMemoryDevice({revisionAddress, revisionAddress, nullptr, readRevision, nullptr});
    if (result) return result;
    initialized = true;
  }
  videoRevision = revision;
  return 0;
}

void shutdownSti5512Devices() {
  if (!initialized) return;
  unregisterMemoryDevice(revisionAddress);
  initialized = false;
}