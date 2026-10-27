#ifndef STI5512_DISPLAY_H
#define STI5512_DISPLAY_H

#include <string>

/* STi5512 display timing and OSD output. Fields are 50 Hz PAL (720x576). */
void resetSti5512Display();
/* raises VSYNC top/bottom events at field boundaries; returns the video interrupt line */
bool tickSti5512Display();
/* renders the background colour and the OSD regions as a 24-bit BMP; returns an error
 * message, or an empty string on success */
std::string writeSti5512Screen(const std::string &path, int *regions = nullptr);

#endif
