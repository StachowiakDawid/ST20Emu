#ifndef ST20_H
#define ST20_H

#pragma once

#include "../../common/defines.h"

/* this is the starting address for CPU execution */
#define START_ADDR 0x7FFFFFFE
#define START_ADDR_CH "START_ADDR"

#define ST20_PRODUCT_ID 0x2D4C9041
#define ST20_PRODUCT_ID_CH "ST20_PRODUCT_ID"

#define MEM_START_VAL 0x80000140
#define MEM_START_VAL_CH "MEM_START_VAL"

#define MINIMUM_INTEGER 0x80000000
#define MAXIMUM_INTEGER 0x7FFFFFFF

#define TIMER_GUESS 0x00000000
#define TIMER_GUESS_CH "TIMER_GUESS"

#define LOW_PRIORITY 1
#define HIGH_PRIORITY 0
#define NOT_PROCESS MINIMUM_INTEGER

/* defines for 16 bit calculations */
#define NBITS 16
#define MAX_INT ((1 << NBITS) - 1)

/*
 * I have no idea where they store the Wptr words.
 * I'll pretend that they're at WPTR_END_ADDR
 * and that they work downwards in memory
 */
#define WPTR_END_ADDR 0x1FFFFFFF
#define WPTR_END_ADDR_CH "WPTR_END_ADDR"
#define MAX_WPTR 0x2000
#define WPTR_PRINT_COLS 4

/* we map the lddevid opcode to a special code
 * to avoid conflicts with the ldprodid code
 */
#define LDDEVID 0x200

/* Error codes from st20.c */
#define ST20_ERROR_START -2000
#define ST20_ERROR_END -2999

#define BAD_WPTR -2000
#define WPTR_UNUSED -2001
#define WPTR_UNDERFLOW -2002
#define WPTR_OVERFLOW -2003
#define BAD_WATCH_CONDITION -2004
#define INVALID_CPU_FILENAME -2010
#define INVALID_CPU_FILE -2011
#define INVALID_CPU_WRITE -2012
#define INVALID_CPU_READ -2013

struct CPUSTATE {
  long areg{0};
  long breg{0};
  long creg{0};
  long iptr{0};
  long status{0};
  int nWptr{0};
  /*  long wptr[MAX_WPTR];*/
  bool wptrUsed[MAX_WPTR]{false};
};

typedef struct watch_struct {
  bool watchAreg;
  long areg;
  bool watchBreg;
  long breg;
  bool watchCreg;
  long creg;
  bool watchIptr;
  long iptr;
  bool watchNWptr;
  int nWptr;
} WATCH;

int st20Init(PARMS *);
int initCPUState(void);
int loadCPUState(const char *);
int saveCPUState(const char *);
int printCPUState();
int printEnablesRegState();
int printOMRState();
int decodeNextInstr();
int printNextInstr();
int execInstr(int *);
int setAreg(long);
int setBreg(long);
int setCreg(long);
int setIptr(long);
int storeWptrWord(long, long);
int initWatch();
int setWatch(const char *, const char *);
bool anyWatch();
bool checkWatch();
const char *st20Error(int);

int addrWptrWord(long, long *);
int wptrPopState(void);
int wptrPushState(void);
int allocWptr(long);
unsigned long N_Add(unsigned long A, unsigned long B, unsigned long *C);
long get_iptr(void);

#endif
