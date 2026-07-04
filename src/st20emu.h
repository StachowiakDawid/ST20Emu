#pragma once

#include <cstdint>

#include "utils/compat.h"

#define ST20_ERROR_START -2000
#define ST20_ERROR_END -2999
#define MEMORY_ERROR_START -1000
#define MEMORY_ERROR_END -1999

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
