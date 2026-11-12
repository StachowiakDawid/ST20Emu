#include "serial_console.h"

#include <cerrno>
#include <csignal>
#include <cstdlib>

#include <poll.h>
#include <termios.h>
#include <unistd.h>

namespace console {

namespace {

termios savedTerminal{};
volatile std::sig_atomic_t terminalChanged = 0;
bool inputEnded = false;
constexpr int FATAL_SIGNALS[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGABRT};

void restoreTerminal() {
  if (terminalChanged) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &savedTerminal);
    terminalChanged = 0;
  }
}

extern "C" void restoreAndRaise(int signal) {
  restoreTerminal(); // tcsetattr is async-signal-safe
  std::signal(signal, SIG_DFL);
  std::raise(signal);
}

} // namespace

void beginSession() {
  if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &savedTerminal) != 0) return;
  termios raw = savedTerminal;
  /* raw input: no line editing, echo, signal keys or CR translation (the target's tty does
   * those); output processing stays on, so the other ports' prefixed lines still end in CR LF */
  raw.c_iflag &= ~static_cast<tcflag_t>(BRKINT | ICRNL | INLCR | IGNCR | INPCK | ISTRIP | IXON);
  raw.c_lflag &= ~static_cast<tcflag_t>(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cflag |= CS8;
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;
  for (int signal : FATAL_SIGNALS) std::signal(signal, restoreAndRaise);
  static const bool exitHandler = std::atexit(restoreTerminal) == 0;
  (void)exitHandler;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0) terminalChanged = 1;
}

void endSession() {
  restoreTerminal();
  for (int signal : FATAL_SIGNALS) std::signal(signal, SIG_DFL);
}

Input pollInput(std::vector<uint8_t> &bytes) {
  if (inputEnded) return Input::None;
  pollfd descriptor{STDIN_FILENO, POLLIN, 0};
  if (poll(&descriptor, 1, 0) <= 0) return Input::None;
  uint8_t buffer[256];
  ssize_t count = read(STDIN_FILENO, buffer, sizeof(buffer));
  if (count < 0 && (errno == EINTR || errno == EAGAIN)) return Input::None;
  if (count <= 0) {
    inputEnded = true;
    return Input::Ended;
  }
  return mapInput(buffer, static_cast<std::size_t>(count), bytes) ? Input::Escape : Input::Bytes;
}

} // namespace console
