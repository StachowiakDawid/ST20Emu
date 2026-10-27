#ifndef DSR9500_H
#define DSR9500_H

#include <string>
#include <string_view>

/* Samsung DSR9500 board parts outside the STi5512; requires initSti5512Devices() first. */
int initDsr9500Board();

/* Sends one NEC remote-control frame. KEY is a name (0-9, ok, menu, exit, up, down, left,
 * right, pgup, pgdn, power) or a command number. Returns an error message, or "" on success. */
std::string dsr9500SendIr(std::string_view key);
/* Holds a front-panel key long enough for the firmware's debounce. KEY is up, down, power or
 * a matrix column bit (1, 2, 4, 8, 0x80). Returns an error message, or "" on success. */
std::string dsr9500PressFrontPanel(std::string_view key);
/* Text currently shown on the 4-digit front-panel display. */
std::string dsr9500Display();
/* The four digits' active-low segment bytes, as hex. */
std::string dsr9500DisplaySegments();

#endif
