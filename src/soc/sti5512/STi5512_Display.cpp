#include "STi5512_Display.h"
#include "STi5512_RegisterBlocks.h"
#include "STi5512_VideoDecoder.h"
#include "../common/STi55xx_Display.h"

/*
 * STi5512 datasheet (7145560 B) section 21.5 and Register Manual (7145562 B) VID/OSD
 * registers. VSYNC top/bottom (VID_ITS 0x63 bits VST and VSB) alternate every PAL field; the
 * OSD starts at OSD_TOP/OSD_BOT and honours OSD_CFG.LLG.
 */
namespace {
constexpr uint32_t vsyncTop = 0x40;
constexpr uint32_t vsyncBottom = 0x20;
constexpr DisplayConfig display{sti5512VideoRegister, sti5512OsdPointer, true, false};
DisplayTiming timing;
}

void resetSti5512Display() {
  resetDisplayTiming(timing);
}

bool tickSti5512Display() {
  Vsync vsync = tickDisplayTiming(timing);
  if (vsync != Vsync::None) {
    sti5512VideoDecoderVsync();
    raiseSti5512VideoInterrupt(vsync == Vsync::Bottom ? vsyncBottom : vsyncTop);
  }
  tickSti5512VideoDecoder();
  return sti5512VideoInterruptLine();
}

std::string writeSti5512Screen(const std::string &path, int *regions) {
  return writeOsdScreen(display, path, regions);
}
