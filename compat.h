#pragma once

#include <cstdio>
#include <format>
#include <string>
#include <string_view>
#include <utility>

// C++23 <print> fallback emulator
namespace compat {
template <typename... Args>
void println(std::FILE *stream, std::format_string<Args...> fmt, Args &&...args) {
  std::string str = std::format(fmt, std::forward<Args>(args)...) + '\n';
  std::fputs(str.c_str(), stream);
}

template <typename... Args> void println(std::format_string<Args...> fmt, Args &&...args) {
  println(stdout, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void print(std::FILE *stream, std::format_string<Args...> fmt, Args &&...args) {
  std::string str = std::format(fmt, std::forward<Args>(args)...);
  std::fputs(str.c_str(), stream);
}

template <typename... Args> void print(std::format_string<Args...> fmt, Args &&...args) {
  print(stdout, fmt, std::forward<Args>(args)...);
}
} // namespace compat

// Helper to efficiently trim whitespace from string_view
constexpr std::string_view trim(std::string_view sv) {
  auto start = sv.find_first_not_of(" \t\r\n");
  if (start == std::string_view::npos)
    return {};
  auto end = sv.find_last_not_of(" \t\r\n");
  return sv.substr(start, end - start + 1);
}

// Helper utility for safe C++23 hexadecimal parsing
template <typename T> static bool parseHex(const std::string &str, T &value) {
  if (str.empty())
    return false;
  auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value, 16);
  return ec == std::errc{};
}
