#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <fstream>
#include <string_view>
#include <string>
#include <system_error>
#include <utility>
#include <strings.h> // POSIX strcasecmp

#include "ncurses.h"
#include "defines.h"

#include "commands.h"
#include "memory.h"
#include "st20.h"

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

uint64_t maxInstr = MAX_UNPROMPTED_INSTR;
uint64_t warnInstr = WARN_UNPROMPTED_INSTR;
uint64_t undefinedWord = UNDEFINED_WORD;

void printError(int error) {
  if (error <= ST20_ERROR_START && error >= ST20_ERROR_END) {
    compat::println(stderr, "ERROR ({}) {}", error, st20Error(error));
  } else if (error <= MEMORY_ERROR_START && error >= MEMORY_ERROR_END) {
    compat::println(stderr, "ERROR ({}) {}", error, memoryError(error));
  } else if (error <= COMMAND_ERROR_START && error >= COMMAND_ERROR_END) {
    compat::println(stderr, "ERROR ({}) {}", error, commandError(error));
  } else {
    compat::println(stderr, "Unknown error ({})", error);
  }
}

void readParms(PARMS *userParms) {
  userParms->nParms = 0;

  std::ifstream parmFp{INI_FILE};
  if (!parmFp) {
    compat::println(stderr, "Cannot open INI file {}\nUsing program defaults", INI_FILE);
    return;
  }

  std::string line;
  while (std::getline(parmFp, line)) {
    std::string_view sv = trim(line);

    // Ignore empty lines and comments
    if (sv.empty() || sv.front() == COMMENT_CHAR) {
      continue;
    }

    auto eqPos = sv.find('=');
    if (eqPos == std::string_view::npos) {
      continue;
    }

    std::string_view key = trim(sv.substr(0, eqPos));
    std::string_view val = trim(sv.substr(eqPos + 1));

    if (key.empty() || val.empty()) {
      continue;
    }

    // Safely copy to the legacy C-struct fixed buffers
    auto copySafe = [](char *dest, std::string_view src, size_t max_size) {
      size_t len = std::min(src.size(), max_size - 1);
      std::memcpy(dest, src.data(), len);
      dest[len] = '\0'; // Ensure null-termination
    };

    copySafe(userParms->parameter[userParms->nParms], key, PARM_SIZE);
    copySafe(userParms->value[userParms->nParms], val, PARM_SIZE);

    userParms->nParms++;

    if (userParms->nParms >= MAX_PARMS) {
      compat::println(stderr, "WARNING: INI file exceeds max parameters ({})", MAX_PARMS);
      break;
    }
  }
}

void st20emuInit(const PARMS *userParms) {
  for (int i{0}; i < userParms->nParms; ++i) {
    uint64_t value{0};
    std::string_view valStr{userParms->value[i]};
    int base{10};

    if (valStr.starts_with("0x") || valStr.starts_with("0X")) {
      valStr.remove_prefix(2);
      base = 16;
    }

    auto [ptr, ec] = std::from_chars(valStr.data(), valStr.data() + valStr.size(), value, base);

    if (ec == std::errc{}) {
      if (!strcasecmp(userParms->parameter[i], MAX_UNPROMPTED_INSTR_CH)) {
        maxInstr = value;
      } else if (!strcasecmp(userParms->parameter[i], WARN_UNPROMPTED_INSTR_CH)) {
        warnInstr = value;
      } else if (!strcasecmp(userParms->parameter[i], UNDEFINED_WORD_CH)) {
        undefinedWord = value;
      }
    }
  }

  compat::println("MAX_UNPROMPTED_INSTR={}\nWARN_UNPROMPTED_INSTR={}\nUNDEFINED_WORD=0x{:08x}",
                  maxInstr, warnInstr, undefinedWord);
}

int main() {
  int result{0};
  // TODO: refactor this to bool when possible
  int watchTripped{0};
  uint64_t instrCount{0};
  PARMS userParms{};

  readParms(&userParms);

  st20emuInit(&userParms);
  st20Init(&userParms, stdout);
  memoryInit(&userParms, stdout);
  commandsInit(&userParms, stdout);

  compat::println("");

  while (!quitRequested()) {
    execInstr(stdout, &watchTripped);

    /*
     * if the program is running without prompts, exit this state if
     * we have executed lots of instructions and haven't encountered
     * a watch condition or if a key has been pressed.
     */
    if (!needPrompt()) {
      if (++instrCount >= maxInstr) {
        compat::println("We've run {} instr without encountering a watch  curr iptr:{:08x}",
                        instrCount, get_iptr());
        setNeedPrompt(true);
      } else if (instrCount % warnInstr == 0) {
        compat::println("Executed {} of {} instr before next prompt  curr iptr: {:08x}", instrCount,
                        maxInstr, get_iptr());
      }

      if (getch() == 'g') {
        setNeedPrompt(true);
      }
    }

    if (watchTripped) {
      compat::println("Watch condition encountered");
      setNeedPrompt(true);
      watchTripped = false;
    }

    /*
     * print the state of the processor
     * after executing the instruction
     */
    if (needPrompt()) {
      printCPUState(stdout);
    }

    decodeNextInstr(stdout);

    if (needPrompt()) {
      printNextInstr(stdout);

      setNeedCmd(true);
      while (needCmd() && needPrompt()) {

        if ((result = getCommand(stdin, stdout))) {
          printError(result);
        }

        if ((result = execCommand(stdin, stdout))) {
          printError(result);
        }

        /*
         * if a person wants the program to run without prompting, start
         * a counter to make sure control eventually returns to the user
         */
        if (!needPrompt()) {
          instrCount = 0;
        }
      }

      decodeNextInstr(stdout);
    }
  }

  compat::println("Finished\n");
  return 0;
}
