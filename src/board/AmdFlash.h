#ifndef AMDFLASH_H
#define AMDFLASH_H

#include <cstdint>

/* 16-bit AMD-command-set NOR flash (Am29LV160 / M29W160 top boot block) in plain memory at
 * [base, base + size). The array stays ordinary memory; commands are intercepted. */
struct AmdFlashChip {
  uint32_t base;
  uint32_t size;
  uint16_t manufacturer;
  uint16_t device;
  /* reads of an erasing sector that return erase status (DQ7 = 0, DQ6 toggling, DQ3 = 1 after
   * the first two) before the array shows again; 0 = erase completes at once */
  int eraseStatusReads = 0;
};

int initAmdFlash(const AmdFlashChip &chip);
void shutdownAmdFlash();

#endif
