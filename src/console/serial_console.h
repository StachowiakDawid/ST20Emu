#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

/* --console PORT: the host terminal as the emulated ASC port's serial terminal */

namespace console {

/* Ctrl-] (as in telnet) ends the console session; it is never sent to the target */
inline constexpr uint8_t ESCAPE_BYTE = 0x1d;

/*
 * Appends the bytes typed on the host to `out` for the target's receive queue and returns true
 * when the escape byte was typed (bytes after it are dropped). Everything else passes unchanged:
 * Enter arrives as CR from a raw terminal (a Linux tty's ICRNL turns it into a newline), LF from
 * a pipe, Backspace as DEL, Ctrl-C as 0x03 (the target's own interrupt character).
 */
inline bool mapInput(const uint8_t *data, std::size_t size, std::vector<uint8_t> &out) {
  for (std::size_t index = 0; index < size; ++index) {
    if (data[index] == ESCAPE_BYTE) return true;
    out.push_back(data[index]);
  }
  return false;
}

/* puts stdin (when it is a terminal) into raw input mode until endSession(), restoring it also
 * on fatal signals and exit() */
void beginSession();
void endSession();

enum class Input { None, Bytes, Escape, Ended };
/* reads what is waiting on stdin without blocking; Ended once at end of file, then None */
Input pollInput(std::vector<uint8_t> &bytes);

} // namespace console
