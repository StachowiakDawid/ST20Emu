#ifndef ST20_H
#define ST20_H

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

#ifndef CPUSTATE_STRUCT
#define CPUSTATE_STRUCT
typedef struct cpuState_struct {
  long areg;
  long breg;
  long creg;
  long iptr;
  long status;
  int nWptr;
  /*  long wptr[MAX_WPTR];*/
  char wptrUsed[MAX_WPTR];
} CPUSTATE;
#endif

#ifndef WATCH_STRUCT
#define WATCH_STRUCT
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
#endif

#ifndef INSTRENTRY_STRUCT
#define INSTRENTRY_STRUCT
typedef struct instrEntry_struct {
  const char *mnemonic;
  int (*function)(long);
  char cpucycles; // instruction cpu cycles
} INSTRENTRY;
#endif

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

int adc_(long);
int add_(long);
int ajw_(long);
int and_(long);
int bcnt_(long);
int bitcnt_(long);
int bsub_(long);
int call_(long);
int cj_(long);
int clockenb_(long);
int clockdis_(long);
int devlb_(long);
int devls_(long);
int devlw_(long);
int devsb_(long);
int devss_(long);
int devsw_(long);
int diff_(long);
int div_(long);
int dup_(long);
int eqc_(long);
int gajw_(long);
int gcall_(long);
int gintdis_(long);
int gintenb_(long);
int gt_(long);
int gtu_(long);
int invalidOp_(long);
int j_(long);
int ladd_(long);
int lb_(long);
int lbx_(long);
int ldc_(long);
int ldclock_(long);
int lddevid_(long);
int ldiff_(long);
int ldiv_(long);
int ldl_(long);
int ldlp_(long);
int ldmemstartval_(long);
int ldnl_(long);
int ldnlp_(long);
int ldpi_(long);
int ldpri_(long);
int ldprodid_(long);
int ldtimer_(long);
int ldtraph_(long);
int lmul_(long);
int ls_(long);
int lshl_(long);
int lshr_(long);
int lsub_(long);
int lsum_(long);
int mint_(long);
int move_(long);
int mul_(long);
int nop_(long);
int not_(long);
int or_(long);
/*********************/
int outword_(long);
/*********************/
int pop_(long);
int prod_(long);
int resetch_(long);
int ret_(long);
int rev_(long);
int runp_(long);
int sb_(long);
int shl_(long);
int shr_(long);
int signal_(long);
int ss_(long);
int ssub_(long);
int startp_(long);
int stclock_(long);
int stl_(long);
int stnl_(long);
int stopp_(long);
int sttimer_(long);
int sub_(long);
int sum_(long);
int trapenb_(long);
int trapdis_(long);
int wait_(long);
int wcnt_(long);
int wsub_(long);
int wsubdb_(long);
int xdble_(long);
int xor_(long);
int xsword_(long);

int _(long);

#endif
