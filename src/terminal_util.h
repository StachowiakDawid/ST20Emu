#ifndef TERMINAL_UTIL_H
#define TERMINAL_UTIL_H

#pragma once

#if defined(__unix__) || defined(__APPLE__) || defined(__linux__)
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>

bool pollInterruptKey() {
  struct termios oldt, newt;
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;

  // disable canonical mode (no Enter required) and echo
  newt.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);

  // set non-blocking mode
  int oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
  fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

  int ch = getchar();

  // restore previous terminal state
  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  fcntl(STDIN_FILENO, F_SETFL, oldf);

  return (ch == 'g' || ch == 'G');
}
#else // Windows
#include <conio.h>

bool pollInterruptKey() {
  if (_kbhit()) {
    int ch = _getch();
    return (ch == 'g' || ch == 'G');
  }
  return false;
}
#endif

#endif
