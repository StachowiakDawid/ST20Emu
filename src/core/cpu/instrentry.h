#ifndef INSTRENTRY_H
#define INSTRENTRY_H

#pragma once

#include <cstdint>

typedef struct instrEntry_struct {
  const char *mnemonic;
  int (*function)(uint32_t);
  uint32_t cpucycles;
} INSTRENTRY;

#endif
