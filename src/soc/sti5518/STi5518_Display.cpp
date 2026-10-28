#include "STi5518_Display.h"
#include "STi5518_RegisterBlocks.h"
#include "../common/STi55xx_Display.h"

/*
 * STi5518 datasheet (7170179 D) section 18.4 and Register Manual (7170180A) VID/OSD
 * registers. VSYNC top/bottom (VID_ITS 0x63 bits VST and VSB) alternate every PAL field; the
 * OSD starts at OSD_OTP/OSD_OBP. The background V component is at 0x99, U at 0x9a.
 */
namespace {
constexpr DisplayConfig display{sti5518VideoRegister, sti5518OsdPointer, false, true};
DisplayTiming timing;
}

void resetSti5518Display() {
  resetDisplayTiming(timing);
}

bool tickSti5518Display() {
  Vsync vsync = tickDisplayTiming(timing);
  if (vsync != Vsync::None) raiseSti5518VideoInterrupt(vsyncInterruptEvent(vsync));
  return sti5518VideoInterruptLine();
}

std::string writeSti5518Screen(const std::string &path, int *regions) {
  return writeOsdScreen(display, path, regions);
}
