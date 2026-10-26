#ifndef STI55XX_INC_H
#define STI55XX_INC_H

#include <cstdint>

/* interrupt trigger modes of the STi55xx interrupt controller (INC_TriggerMode) */
inline bool triggered(uint32_t mode, bool current, bool previous) {
  switch (mode & 7U) {
    case 1: return current;
    case 2: return !current;
    case 3: return current && !previous;
    case 4: return !current && previous;
    case 5: return current != previous;
    default: return false;
  }
}

#endif
