#ifndef MAIN_H
#define MAIN_H

#pragma once

#include "common/compat.h"

#include <cstdint>

constexpr const char *INI_FILE{"st20emu.ini"};
constexpr const char COMMENT_CHAR{'#'};

constexpr uint64_t MAX_UNPROMPTED_INSTR{1000000};
constexpr const char *MAX_UNPROMPTED_INSTR_CH{"MAX_UNPROMPTED_INSTR"};
constexpr uint64_t WARN_UNPROMPTED_INSTR{100000};
constexpr const char *WARN_UNPROMPTED_INSTR_CH{"WARN_UNPROMPTED_INSTR"};
constexpr uint64_t UNDEFINED_WORD{0xCCCCCCCC};
constexpr const char *UNDEFINED_WORD_CH{"UNDEFINED_WORD"};

#endif
