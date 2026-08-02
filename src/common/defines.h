#ifndef DEFINES_H
#define DEFINES_H

#pragma once

inline constexpr unsigned long UNDEFINED_WORD_OLD = 0xCCCCCCCCU;

inline constexpr int NAME_SIZE = 100;
inline constexpr int PARM_SIZE = 30;
inline constexpr int MAX_PARMS = 50;

struct PARMS {
  int nParms{0};
  char parameter[MAX_PARMS][PARM_SIZE]{};
  char value[MAX_PARMS][PARM_SIZE]{};
};

#endif
