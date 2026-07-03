#pragma once

#include <cstdint>
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
} // namespace compat

// Helper to efficiently trim whitespace from string_view
constexpr std::string_view trim(std::string_view sv) {
  auto start = sv.find_first_not_of(" \t\r\n");
  if (start == std::string_view::npos)
    return {};
  auto end = sv.find_last_not_of(" \t\r\n");
  return sv.substr(start, end - start + 1);
}

#define ST20_ERROR_START -2000
#define ST20_ERROR_END -2999
#define MEMORY_ERROR_START -1000
#define MEMORY_ERROR_END -1999
#define COMMAND_ERROR_START -3000
#define COMMAND_ERROR_END -3999

// TODO: replace macros with this later
// constexpr int ST20_ERROR_START{-2000};
// constexpr int ST20_ERROR_END{-2999};
// constexpr int MEMORY_ERROR_START{-1000};
// constexpr int MEMORY_ERROR_END{-1999};
// constexpr int COMMAND_ERROR_START{-3000};
// constexpr int COMMAND_ERROR_END{-3999};

constexpr const char *INI_FILE{"st20emu.ini"};
constexpr const char COMMENT_CHAR{'#'};

constexpr uint64_t MAX_UNPROMPTED_INSTR{1000000};
constexpr const char *MAX_UNPROMPTED_INSTR_CH{"MAX_UNPROMPTED_INSTR"};
constexpr uint64_t WARN_UNPROMPTED_INSTR{100000};
constexpr const char *WARN_UNPROMPTED_INSTR_CH{"WARN_UNPROMPTED_INSTR"};
constexpr uint64_t UNDEFINED_WORD{0xCCCCCCCC};
constexpr const char *UNDEFINED_WORD_CH{"UNDEFINED_WORD"};
