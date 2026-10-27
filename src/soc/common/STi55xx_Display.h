#ifndef STI55XX_DISPLAY_H
#define STI55XX_DISPLAY_H

#include <cstdint>
#include <string>

/*
 * Display timing and OSD output shared by the STi5512 and STi5518, whose OSD block headers,
 * palettes and OSD_CFG/background registers match (STi5512 datasheet 7145560 B section 21.5,
 * STi5518 datasheet 7170179 D section 18.4). Fields are 50 Hz PAL (720x576). The MPEG video
 * and still-picture planes are not decoded, so the OSD is mixed over the background colour.
 */

/* the SoC's video registers (VideoBaseAddress 0) and its differences between the chips */
struct DisplayConfig {
  /* stored value of a video register */
  uint8_t (*videoRegister)(uint32_t offset);
  /* first OSD block of the top (0) or bottom (1) field, in units of 256 bytes of SDRAM */
  uint32_t (*osdPointer)(int field);
  /* OSD_CFG.LLG (bit 4) exists: when set, OSD pointers address alternate words (STi5512) */
  bool linkedListGranularity;
  /* the background V component is at 0x99 and U at 0x9a (STi5518) instead of U then V */
  bool backgroundVFirst;
};

enum class Vsync { None, Top, Bottom };

struct DisplayTiming {
  uint64_t nextField = 0;
  bool bottomField = false;
};

void resetDisplayTiming(DisplayTiming &timing);
/* reports the VSYNC of a field boundary that has passed since the last call */
Vsync tickDisplayTiming(DisplayTiming &timing);
/* renders the background colour and the OSD regions as a 24-bit BMP; returns an error
 * message, or an empty string on success */
std::string writeOsdScreen(const DisplayConfig &config, const std::string &path, int *regions = nullptr);

#endif
