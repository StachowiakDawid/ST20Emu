#ifndef MAIN_H
#define MAIN_H

#pragma once

#include "common/compat.h"

#include <cstdint>

/* a free run (g) stops after this many instructions and reports progress every WARN */
constexpr uint64_t MAX_UNPROMPTED_INSTR{10000000};
constexpr uint64_t WARN_UNPROMPTED_INSTR{1000000};

#endif
