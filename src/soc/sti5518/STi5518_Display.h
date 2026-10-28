#ifndef STI5518_DISPLAY_H
#define STI5518_DISPLAY_H

#include <string>

void resetSti5518Display();
/* advances the PAL field timing; returns the video interrupt line */
bool tickSti5518Display();
/* renders the background colour and the OSD regions as a 24-bit BMP; returns an error
 * message, or an empty string on success */
std::string writeSti5518Screen(const std::string &path, int *regions = nullptr);

#endif
