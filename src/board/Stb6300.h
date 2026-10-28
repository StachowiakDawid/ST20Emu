#ifndef STB6300_H
#define STB6300_H

#include <string>
#include <string_view>

/* Board parts outside the STi5518 of the receiver whose flash is firmware/6300.bin;
 * requires initSti5518Devices() first. */
int initStb6300Board();
bool stb6300Active();

/* Sends a key packet from the front-panel controller. KEY is power or a code byte (hex); the
 * power key is also reported after the next handshake, waking the receiver from standby.
 * Returns an error message, or "" on success. */
std::string stb6300SendKey(std::string_view key);
/* Sends one NEC remote-control frame with command KEY: menu or a hex byte. Returns an error
 * message, or "" on success. */
std::string stb6300SendIr(std::string_view key);
/* The display packets last sent to the front-panel controller, as hex. */
std::string stb6300Display();

#endif
