#include "AmdFlash.h"

#include "../core/memory/memory.h"

/*
 * Command set of the Am29LV160D data sheet in word (x16) mode: unlock cycles 0xaa at word
 * 0x555 and 0x55 at word 0x2aa, then 0x90 autoselect, 0xa0 program one word, 0x80 erase
 * (two more unlock cycles, then 0x30 at a sector or 0x10 at 0x555 for the whole chip), 0x20
 * unlock bypass (then 0xa0 + data programs a word without unlock cycles, 0x90 0x00 leaves it);
 * 0xf0 anywhere returns to reading the array. Programming completes at once, so DQ7/DQ6 polls
 * see the final data; erasing does too unless the chip sets eraseStatusReads, in which case
 * that many reads return erase status first (the DSR9500's erase routine wants DQ7 = 0 right
 * after the command and DQ3 = 1 before it waits for DQ7); meanwhile 0x30 written to another
 * sector adds it to the erase, as in the chip's sector erase time-out. Only 11 address bits are decoded for unlock
 * cycles, so 0x5555/0x2aaa (8-bit-style software) also match. Other writes are ignored, as
 * on the chip, and never change the array. Autoselect reads return the manufacturer code at
 * word 0, the device code at word 1 and 0 (unprotected) at word 2 of each sector.
 */
namespace {
AmdFlashChip flash{};
bool active = false;
bool autoselect = false;
int cycle = 0;       // unlock cycles seen
bool erasePrefix = false;
bool programNext = false;
bool bypass = false;
bool bypassExit = false;
int statusReads = 0; // erase status reads left
int statusCount = 0;

bool unlockAddress(uint32_t offset, uint32_t word) {
  return ((offset >> 1) & 0x7ffU) == (word & 0x7ffU);
}

/* sector containing OFFSET: 64 KB sectors, the top one split 32+8+8+16 KB */
void sectorRange(uint32_t offset, uint32_t *start, uint32_t *length) {
  uint32_t top = flash.size - 0x10000U;
  if (offset < top) {
    *start = offset & ~0xffffU;
    *length = 0x10000U;
    return;
  }
  static constexpr uint32_t starts[] = {0, 0x8000, 0xa000, 0xc000, 0x10000};
  for (int index = 0; index < 4; ++index) {
    if (offset - top < starts[index + 1]) {
      *start = top + starts[index];
      *length = starts[index + 1] - starts[index];
      return;
    }
  }
}

void erase(uint32_t start, uint32_t length) {
  for (uint32_t offset = 0; offset < length; offset += 4) storeBytes(flash.base + start + offset, 4, UINT32_MAX);
}

int readAutoselect(void *, uint32_t address, int width, uint32_t *value) {
  uint32_t offset = address - flash.base;
  uint32_t word = 0;
  for (int byte = 0; byte < width; ++byte) {
    uint32_t at = offset + static_cast<uint32_t>(byte);
    uint32_t index = (at >> 1) & 0xffU;
    uint16_t code = index == 0 ? flash.manufacturer : index == 1 ? flash.device : 0;
    word |= ((at & 1U) ? code >> 8 : code & 0xffU) << (8 * byte);
  }
  *value = word;
  return 0;
}

int readEraseStatus(void *, uint32_t, int width, uint32_t *value);

void setStatusDevice(bool enable) {
  if (enable) registerMemoryDevice({flash.base, flash.base + flash.size - 1U, nullptr, readEraseStatus, nullptr});
  else unregisterMemoryDevice(flash.base);
}

int readEraseStatus(void *, uint32_t, int width, uint32_t *value) {
  ++statusCount;
  uint32_t status = (statusCount & 1 ? 0x40U : 0) | (statusCount > 2 ? 0x08U : 0);
  *value = status;
  if (width > 2) *value |= status << 16;
  if (--statusReads <= 0) setStatusDevice(false);
  return 0;
}

void startEraseStatus() {
  if (flash.eraseStatusReads <= 0 || statusReads > 0 || autoselect) return;
  statusReads = flash.eraseStatusReads;
  statusCount = 0;
  setStatusDevice(true);
}

void setAutoselect(bool enable) {
  if (enable == autoselect) return;
  autoselect = enable;
  if (enable) registerMemoryDevice({flash.base, flash.base + flash.size - 1U, nullptr, readAutoselect, nullptr});
  else unregisterMemoryDevice(flash.base);
}

bool flashWrite(uint32_t address, int width, uint32_t value) {
  uint32_t offset = address - flash.base;
  uint32_t data = value & 0xffU;
  if (programNext) {
    programNext = false;
    uint32_t old = 0;
    readBytes(address, width, &old);
    storeBytes(address, width, old & value);
    return true;
  }
  if (data == 0xf0U && !bypass) {
    cycle = 0;
    erasePrefix = false;
    setAutoselect(false);
    return true;
  }
  if (statusReads > 0 && data == 0x30U && cycle == 0) {
    /* further sectors written with 0x30 while the erase is pending join it */
    uint32_t start = 0, length = 0;
    sectorRange(offset, &start, &length);
    setStatusDevice(false); // the status reads would also take the erase's stores
    erase(start, length);
    setStatusDevice(true);
    statusReads = flash.eraseStatusReads; // the time-out window restarts
    statusCount = 0;
    return true;
  }
  if (bypass) {
    if (bypassExit) {
      bypassExit = false;
      if (data == 0) bypass = false;
    } else if (data == 0xa0U) {
      programNext = true;
    } else if (data == 0x90U) {
      bypassExit = true;
    }
    return true;
  }
  if (cycle == 0 && data == 0xaaU && unlockAddress(offset, 0x555)) cycle = 1;
  else if (cycle == 1 && data == 0x55U && unlockAddress(offset, 0x2aa)) cycle = 2;
  else if (cycle == 2) {
    cycle = 0;
    if (erasePrefix) {
      erasePrefix = false;
      if (data == 0x10U && unlockAddress(offset, 0x555)) erase(0, flash.size);
      else if (data == 0x30U) {
        uint32_t start = 0, length = 0;
        sectorRange(offset, &start, &length);
        erase(start, length);
        startEraseStatus();
      }
    } else if (data == 0x90U && unlockAddress(offset, 0x555)) setAutoselect(true);
    else if (data == 0xa0U && unlockAddress(offset, 0x555)) programNext = true;
    else if (data == 0x80U && unlockAddress(offset, 0x555)) erasePrefix = true;
    else if (data == 0x20U && unlockAddress(offset, 0x555)) bypass = true;
  } else cycle = 0;
  return true;
}
}

int initAmdFlash(const AmdFlashChip &chip) {
  shutdownAmdFlash();
  flash = chip;
  cycle = 0;
  erasePrefix = programNext = autoselect = bypass = bypassExit = false;
  statusReads = 0;
  setWriteInterceptor(chip.base, chip.base + chip.size - 1U, flashWrite);
  active = true;
  return 0;
}

void shutdownAmdFlash() {
  if (!active) return;
  setAutoselect(false);
  if (statusReads > 0) setStatusDevice(false);
  statusReads = 0;
  setWriteInterceptor(1, 0, nullptr);
  active = false;
}
