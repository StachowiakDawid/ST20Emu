#ifndef INSTRENTRY_H
#define INSTRENTRY_H

#pragma once

typedef struct instrEntry_struct {
  const char *mnemonic;
  int (*function)(long);
  char cpucycles; // instruction cpu cycles
} INSTRENTRY;

#endif
