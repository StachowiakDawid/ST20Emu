#ifndef COMPAT_H
#define COMPAT_H

#pragma once

#include <format>
#include <iterator>
#include <string_view>
#include <string>
#include <utility>

#include <cstdio>

/* Formatted output used by every module. Output can be collected in a per-thread buffer (the
 * batch log), and per-access trace notes can be turned off (free runs). */
namespace compat {
inline thread_local std::string *outputBuffer = nullptr;

template <typename... Args>
void println(std::FILE *stream, std::format_string<Args...> fmt, Args &&...args) {
  if (outputBuffer && (stream == stdout || stream == stderr)) {
    std::format_to(std::back_inserter(*outputBuffer), fmt, std::forward<Args>(args)...);
    outputBuffer->push_back('\n');
  } else {
    std::string str = std::format(fmt, std::forward<Args>(args)...) + '\n';
    std::fputs(str.c_str(), stream);
  }
}

template <typename... Args> void println(std::format_string<Args...> fmt, Args &&...args) {
  println(stdout, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void print(std::FILE *stream, std::format_string<Args...> fmt, Args &&...args) {
  if (outputBuffer && (stream == stdout || stream == stderr)) {
    std::format_to(std::back_inserter(*outputBuffer), fmt, std::forward<Args>(args)...);
  } else {
    std::string str = std::format(fmt, std::forward<Args>(args)...);
    std::fputs(str.c_str(), stream);
  }
}

template <typename... Args> void print(std::format_string<Args...> fmt, Args &&...args) {
  compat::print(stdout, fmt, std::forward<Args>(args)...);
}

// per-instruction trace notes (memory and device accesses); free-running execution turns them off
inline bool notesEnabled = true;

template <typename... Args> void note(std::format_string<Args...> fmt, Args &&...args) {
  if (notesEnabled) println(stdout, fmt, std::forward<Args>(args)...);
}
} // namespace compat

#endif
