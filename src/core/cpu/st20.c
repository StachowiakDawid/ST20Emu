/********************************************************************/
/* 16/12/2011	-Fixed 'bitcnt' instruction							*/
/*				-Added some detailed instruction comment			*/
/* 19/12/2011   -Added 'db' (query database) command				*/
/* 20/12/2011	-Added 'vra' (verbose register access) command		*/
/*				 that show info's also in 'go' mode					*/
/*				 (added a status flag into struct cmdState_struct)	*/
/*				-fixed some C4996 warnings about float->long and	*/
/*				 long->float. A cast has been enough.				*/
/*				-implemented binary search trough db entries		*/
/*				 and put all registers in a more suitable form		*/
/* 23/12/2011   -check all registers entries order.					*/
/*				-create Defines for 'h2idc.idc' IDA Script		    */
/*				-compiled with static libraries.					*/
/* 26/12/2011	-'g' command can be interrupted hitting the 'g' key */
/*				-added a less detailed description based upon the   */
/*				 STi5518 memory map									*/
/* 31/12/2011	-new year's eve :)									*/
/*				-added 'omr' command (OtherMachineRegisters)		*/
/*				-added 'ver' command (View Enables Register)		*/
/*				-added cpucycles quasi-precise clock				*/
/*				-added these instructions:							*/
/*					'gintenb/gintdis'								*/
/*					'trapenb/trapdis'								*/
/*					'ldclock/clockenb/clockdis'						*/
/*					'timerenb/timerdis'								*/
/* 04/01/2012	-various bug fixed around the code					*/
/*				-emulation cycle now is better						*/
/*				-HighPriority and LowPriority ClockReg more precise */
/********************************************************************/

// std
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// posix / linux
#include <fcntl.h>
#include <unistd.h>

#include "../../common/defines.h"
#include "../memory/memory.h"
// #include "commands.h" // disabled until ported to C++
#include "st20.h"
// #include "../../soc/sti5518/STi5518_SearchDB.h" //Internal DB facility
#include "omr.h" //Other Machine Registers include file

// cannot use static when sharing with other C objects
OMRSTATE omrState;
CPUSTATE cpuState;
WATCH watch;
long instrCode = 0;
unsigned long operand = (long)START_ADDR;
unsigned char instrBytes[100];
int instrLength = 0;
long st20ProductId = ST20_PRODUCT_ID;
long memStartVal = MEM_START_VAL;
long timerGuess = TIMER_GUESS;
long startAddr = START_ADDR;
long wptrEndAddr = WPTR_END_ADDR;

int initTimer(FILE *outFp);

extern INSTRENTRY instrEntry[];

int st20Init(PARMS *userParms, FILE *outFp) {
  int i;
  long value;

  for (i = 0; i < userParms->nParms; i++) {
    if (!strcmp(userParms->parameter[i], ST20_PRODUCT_ID_CH)) {
      if (sscanf(userParms->value[i], "%lx", &value) == 1) {
        st20ProductId = value;
      }
    } else if (!strcmp(userParms->parameter[i], MEM_START_VAL_CH)) {
      if (sscanf(userParms->value[i], "%lx", &value) == 1) {
        memStartVal = value;
      }
    } else if (!strcmp(userParms->parameter[i], TIMER_GUESS_CH)) {
      if (sscanf(userParms->value[i], "%lx", &value) == 1) {
        timerGuess = value;
      }
    } else if (!strcmp(userParms->parameter[i], WPTR_END_ADDR_CH)) {
      if (sscanf(userParms->value[i], "%lx", &value) == 1) {
        wptrEndAddr = value;
      }
    } else if (!strcmp(userParms->parameter[i], START_ADDR_CH)) {
      if (sscanf(userParms->value[i], "%lx", &value) == 1) {
        startAddr = value;
      }
    }
  }

  fprintf(outFp, "START_ADDR=0x%08lx\nMEM_START_VAL=0x%08lx\nST20_PRODUCT_ID=0x%08lx\n\
TIMER_GUESS=0x%08lx\nWPTR_END_ADDR=0x%08lx\n",
          startAddr, memStartVal, st20ProductId, timerGuess, wptrEndAddr);

  initCPUState();
  initWatch();
  // added for timers
  initTimer(outFp);

  return (0);
}

/**************************
 * Initialize the processor
 */
int initCPUState(void) {
  int i;
  int result = 0;
  long address;

  /* prepare to start the processing at 0x7FFFFFFE */
  instrCode = 0;
  operand = (long)startAddr;
  instrLength = 0;

  /* initialize the state of the processor */
  cpuState.nWptr = 7;
  cpuState.wptrUsed[0] = TRUE;
  cpuState.wptrUsed[1] = TRUE;
  cpuState.wptrUsed[2] = TRUE;
  cpuState.wptrUsed[3] = TRUE;
  cpuState.wptrUsed[4] = TRUE;
  cpuState.wptrUsed[5] = TRUE;
  cpuState.wptrUsed[6] = TRUE;
  addrWptrWord((int)0, &address);
  result = allocBytes(address - 4 * 7, 4 * 7);
  /*  cpuState.wptr[0] = UNDEFINED_WORD_OLD;*/

  for (i = 7; i < MAX_WPTR; i++) {
    cpuState.wptrUsed[i] = FALSE;
  }
  cpuState.areg = cpuState.breg = cpuState.creg = UNDEFINED_WORD_OLD;
  cpuState.iptr = 0;

  return (result);
}

int initWatch(void) {
  watch.watchAreg = FALSE;
  watch.watchBreg = FALSE;
  watch.watchCreg = FALSE;
  watch.watchIptr = FALSE;
  watch.watchNWptr = FALSE;

  return (0);
}

int saveCPUState(const char *dirName, FILE *outFp) {
  char cpuFileName[NAME_SIZE];
  int cpuFileFd = -1;

  if (sprintf(cpuFileName, "%s/cpu.bin", dirName) == EOF) {
    return (INVALID_CPU_FILENAME);
  }
#if defined(S_IREAD) && defined(S_IWRITE)
  if ((cpuFileFd = open(cpuFileName, O_WRONLY | O_CREAT | O_TRUNC, S_IREAD | S_IWRITE)) < 0) {
    perror("CPU state save file cannot be opened");
    return (INVALID_CPU_FILE);
  }
#else
  if ((cpuFileFd = open(cpuFileName, O_WRONLY | O_CREAT | O_TRUNC)) < 0) {
    perror("CPU state save file cannot be opened");
    return (INVALID_CPU_FILE);
  }
#endif

  if (write(cpuFileFd, (void *)&cpuState, sizeof(CPUSTATE)) < 0) {
    return (INVALID_CPU_WRITE);
  }

  if (write(cpuFileFd, (void *)&wptrEndAddr, 4) < 0) {
    return (INVALID_CPU_WRITE);
  }
  if (write(cpuFileFd, (void *)&watch, sizeof(WATCH)) < 0) {
    return (INVALID_CPU_WRITE);
  }

  close(cpuFileFd);

  return (0);
}

long get_iptr(void) {
  return (cpuState.iptr);
}

int loadCPUState(const char *dirName, FILE *outFp) {
  char cpuFileName[NAME_SIZE];
  int cpuFileFd = -1;

  if (sprintf(cpuFileName, "%s/cpu.bin", dirName) == EOF) {
    return (INVALID_CPU_FILENAME);
  }
#if defined(S_IREAD) && defined(S_IWRITE)
  if ((cpuFileFd = open(cpuFileName, O_RDONLY, S_IREAD | S_IWRITE)) < 0) {
    perror("CPU state save file cannot be opened");
    return (INVALID_CPU_FILE);
  }
#else
  if ((cpuFileFd = open(cpuFileName, O_RDONLY)) < 0) {
    perror("CPU state save file cannot be opened");
    return (INVALID_CPU_FILE);
  }
#endif

  if (read(cpuFileFd, &cpuState, sizeof(CPUSTATE)) <= 0) {
    return (INVALID_CPU_READ);
  }
  if (read(cpuFileFd, &wptrEndAddr, 4) <= 0) {
    return (INVALID_CPU_READ);
  }

  if (read(cpuFileFd, &watch, sizeof(WATCH)) <= 0) {
    return (INVALID_CPU_READ);
  }

  close(cpuFileFd);

  return (0);
}

int setWatch(const char *reg, const char *parm) {
  int enable;
  long value;

  if (strcmp(parm, "clear")) {
    enable = TRUE;
  } else {
    enable = FALSE;
  }
  sscanf(parm, "%lx", &value);

  switch (reg[0]) {
  case 'a':
  case 'A':
    watch.watchAreg = enable;
    watch.areg = value;
    break;

  case 'b':
  case 'B':
    watch.watchBreg = enable;
    watch.breg = value;
    break;

  case 'c':
  case 'C':
    watch.watchCreg = enable;
    watch.creg = value;
    break;

  case 'i':
  case 'I':
    watch.watchIptr = enable;
    watch.iptr = value;
    break;

  case 'w':
  case 'W':
    watch.watchNWptr = enable;
    watch.nWptr = value;
    break;

  default:
    return (BAD_WATCH_CONDITION);
    break;
  }

  return (0);
}

int anyWatch() {
  return (watch.watchAreg || watch.watchBreg || watch.watchCreg || watch.watchIptr ||
          watch.watchNWptr);
}

int checkWatch() {
  int result = FALSE;

  result |= (int)(watch.watchAreg && (cpuState.areg == watch.areg));
  result |= (int)(watch.watchBreg && (cpuState.breg == watch.breg));
  result |= (int)(watch.watchCreg && (cpuState.creg == watch.creg));
  result |= (int)(watch.watchIptr && (cpuState.iptr == watch.iptr));
  result |= (int)(watch.watchNWptr && (cpuState.nWptr == watch.nWptr));

  return (result);
}

/**************************
 * Print the values in the processor state
 */
int printCPUState(FILE *outFp) {
  int i;
  int result = 0;
  long address;
  long value;

  fprintf(outFp, "A=0x%08lx B=0x%08lx C=0x%08lx  Iptr=0x%08lx \n", cpuState.areg, cpuState.breg,
          cpuState.creg, cpuState.iptr);

  for (i = 0; i < cpuState.nWptr; i++) {
    if (i == 0) {
      fprintf(outFp, "Wptr");
    }

    addrWptrWord(i, &address);
    result = readBytes(address, 4, (unsigned long *)&value);
    fprintf(outFp, " %2x=0x%08lx", i, value);

    if (i % WPTR_PRINT_COLS == WPTR_PRINT_COLS - 1) {
      fprintf(outFp, "\n    ");
    }
  }

  fprintf(outFp, "\n\n");

  return (result);
}

/**************************
 * Print the values of the OMR
 */
int printOMRState(FILE *outFp) {
  // show the OMR Enables register
  fprintf(outFp, "OTHER MACHINE REGISTERS\n");
  fprintf(outFp, "-----------------------\n");
  fprintf(outFp, "Enables=0x%08lx\n", omrState.Enables);
  fprintf(outFp, "ClockRegHP=0x%08lx ", omrState.ClockRegHP);
  fprintf(outFp, "ClockRegLP=0x%08lx ", omrState.ClockRegLP);
  fprintf(outFp, "ClockEnables=0x%02x \n", omrState.ClockEnables);
  fprintf(outFp, "HP_ErrFlag=0x%02x ", omrState.HP_ErrorFlag);
  fprintf(outFp, "LP_ErrFlag=0x%02x ", omrState.LP_ErrorFlag);
  fprintf(outFp, "HaltOnError=0x%02x\n", omrState.HaltOnErrorFlag);
  return (0);
}
/**************************
 * Print the values in the Enables Register
 */
int printEnablesRegState(FILE *outFp) {

  fprintf(outFp, "Enables Register Value=0x%08lx\n", omrState.Enables);
  // fprintf(outFp,"-GLOBAL INTERRUPTS ENABLES VALUES-\n");
  if (omrState.Enables & LP_PROCESS_INT_ENB)
    fprintf(outFp, " LP_PROCESS_INT_ENB	is set\n");
  if (omrState.Enables & LP_TIMESLICE_ENB)
    fprintf(outFp, " LP_TIMESLICE_ENB	is set\n");
  if (omrState.Enables & LP_EXTERNALEVENT_ENB)
    fprintf(outFp, " LP_EXTERNALEVENT_ENB	is set\n");
  if (omrState.Enables & LP_TIMER_ALRM_ENB)
    fprintf(outFp, " LP_TIMER_ALRM_ENB	is set\n");
  if (omrState.Enables & HP_PROCESS_INT_ENB)
    fprintf(outFp, " HP_PROCESS_INT_ENB	is set\n");
  if (omrState.Enables & HP_TIMESLICE_ENB)
    fprintf(outFp, " HP_TIMESLICE_ENB	is set\n");
  if (omrState.Enables & HP_EXTERNALEVENT_ENB)
    fprintf(outFp, " HP_EXTERNALEVENT_ENB	is set\n");
  if (omrState.Enables & HP_TIMER_ALRM_ENB)
    fprintf(outFp, " HP_TIMER_ALRM_ENB	is set\n");
  // fprintf(outFp,"-TRAP ENABLES VALUES-\n");
  if (omrState.Enables & BREAKPOINT_TRAPENB)
    fprintf(outFp, " BREAKPOINT_TRAPENB	is set\n");
  if (omrState.Enables & INTEGER_ERR_TRAPENB)
    fprintf(outFp, " INTEGER_ERR_TRAPENB	is set\n");
  if (omrState.Enables & INTEGER_OVF_TRAPENB)
    fprintf(outFp, " INTEGER_OVF_TRAPENB	is set\n");
  if (omrState.Enables & ILL_OPCODE_TRAPENB)
    fprintf(outFp, " ILL_OPCODE_TRAPENB	is set\n");
  if (omrState.Enables & LOADTRAP_TRAPENB)
    fprintf(outFp, " LOADTRAP_TRAPENB	is set\n");
  if (omrState.Enables & STORETRAP_TRAPENB)
    fprintf(outFp, " STORETRAP_TRAPENB	is set\n");
  if (omrState.Enables & INTERNALCH_TRAPENB)
    fprintf(outFp, " INTERNALCH_TRAPENB	is set\n");
  if (omrState.Enables & EXTERNALCH_TRAPENB)
    fprintf(outFp, " EXTERNALCH_TRAPENB	is set\n");
  if (omrState.Enables & TIMER_TRAPENB)
    fprintf(outFp, " TIMER_TRAPENB	is set\n");
  if (omrState.Enables & TIMESLICE_TRAPENB)
    fprintf(outFp, " TIMESLICE_TRAPENB	is set\n");
  if (omrState.Enables & RUN_TRAPENB)
    fprintf(outFp, " RUN_TRAPENB	is set\n");
  if (omrState.Enables & SIGNAL_TRAPENB)
    fprintf(outFp, " SIGNAL_TRAPENB	is set\n");
  if (omrState.Enables & PROCESS_TRAPENB)
    fprintf(outFp, " PROCESS_TRAPENB	is set\n");
  if (omrState.Enables & QUEUE_EMPTY_TRAPENB)
    fprintf(outFp, " QUEUE_EMPTY_TRAPENB	is set\n");

  return (0);
}

/**************************
 * Get the bytes for the next instruction and determine what the instruction
 * code and operand are
 */
int decodeNextInstr(FILE *outFp) {
  int foundInstr = FALSE;
  unsigned long cByte;
  int result;

  instrCode = 0;
  operand = 0;
  instrLength = 0;

  /* keep reading a byte until we've found a non-prefix byte */
  while (!foundInstr) {

    /* read the next byte of the current instruction */
    result = readBytes(cpuState.iptr + instrLength, 1, &cByte);
    if (result) {
      fprintf(outFp, "%s\n", memoryError(result));
      fprintf(outFp, "Error occurred when reading instruction at %8lx, offset %2x\n", cpuState.iptr,
              instrLength);
      return (-1);
    }

    /* save the bytes that comprise the current instruction */
    instrBytes[instrLength++] = (char)(cByte & 0xFF);

    /* extract the data part of the current instruction byte */
    operand <<= 4;
    operand += cByte & 0x0F;

    /* extract the instruction part of the current byte */
    instrCode = cByte & 0xF0;
    instrCode >>= 4;

    /*
     * The prefix encoded instructions are multi-byte instructions
     * (either the data or the instruction is composed of multiple bytes).
     * We have to accumulate all of the bytes in the instruction before
     * we can determine what the instruction and the operand are.
     */
    switch (instrCode) {
    case 0x02:
      foundInstr = FALSE;
      break;

    case 0x06:
      operand = ~operand;
      foundInstr = FALSE;
      break;

    /*
     * All multibyte instruction codes use Fx as the final byte in the
     * instruction.  These instructions require no operands so the
     * operand is used as an extended instruction code.
     * We add 0x10 to the instruction code to avoid conflicts with
     * the opcodes from the non-extended instructions.
     * The lddevid code will end up with the same code as ldprodid.
     *  So we'll map it to 0x200 to avoid the conflict.
     */
    case 0x0F:
      if (operand == 0x17C) {
        instrCode = LDDEVID;
      } else {
        instrCode = (operand & 0x1FF) + 0x10;
      }

      foundInstr = TRUE;

      break;

    /* all of the non-Fx single byte instructions are handled here */
    default:
      foundInstr = TRUE;
      break;
    }
  }

  return (0);
}

/**************************
 * Print out the address, bytes and decoded instruction to be executed next
 */
int printNextInstr(FILE *outFp) {
  int i;
  char operandCh[100];

  fprintf(outFp, "%08lx  ", cpuState.iptr);

  for (i = 0; i < instrLength; i++) {
    fprintf(outFp, "%2x ", instrBytes[i]);
  }

  /*
   * for the jump, conditional jump and call instructions, we need to print
   * the address (i.e. iptr + operand) rather than the operand
   */
  if (instrCode == 0x00 || instrCode == 0x0A) {
    sprintf(operandCh, " loc_%08lx", (cpuState.iptr + instrLength + operand) & 0xFFFFFFFF);
  } else if (instrCode == 0x09) {
    sprintf(operandCh, " sub_%08lx", (cpuState.iptr + instrLength + operand) & 0xFFFFFFFF);
  } else if (instrCode > 0x0F) {
    operandCh[0] = '\0';
  } else {
    sprintf(operandCh, " %-8lx", operand & 0xFFFFFFFF);
  }

  fprintf(outFp, " %s%s\n", instrEntry[instrCode].mnemonic, operandCh);

  return (0);
}

//////////////////////////////////////////////////////////////////////////
//					EXEC DECODED INSTRUCTION
//				(Here the we tick the timers too)
//////////////////////////////////////////////////////////////////////////
int execInstr(FILE *outFp, int *breakFlag) {
  int result = 0;

  cpuState.iptr += instrLength; // update iptr

  // I've added this two statements for a cpu cycles more precise timing
  // a big overhead, i know, but in this way the timer is stopped
  // when we are in single instruction mode, without too many efforts.

  //*** HP_TIMER ***
  if (omrState.ClockEnables & HPTIMER_MASK) { // if the TIMER is active do:
    if ((hp_timertick -= instrEntry[instrCode].cpucycles) <= 0) {
      omrState.ClockRegHP++;        // tick every HP_TIMERTICK
      hp_timertick = HP_TIMER_TICK; // reset
    }
  }
  //*** LP_TIMER ***
  if (omrState.ClockEnables & LPTIMER_MASK) { // if the TIMER is active do:
    if ((lp_timertick -= instrEntry[instrCode].cpucycles) <= 0) {
      omrState.ClockRegLP++;        // tick every LP_TIMERTICK
      lp_timertick = LP_TIMER_TICK; // reset
    }
  }

  result = instrEntry[instrCode].function(outFp, operand);

  *breakFlag = checkWatch();

  return (result);
}

/**************************
 */
int readWptrWord(long index, long *value) {
  int result = 0;
  long address;

  if (index >= cpuState.nWptr) {
    *value = 0;
    return (BAD_WPTR);
  }
  /*  if (!cpuState.wptrUsed[cpuState.nWptr - index - 1]) {
     *value = 0;
     return (WPTR_UNUSED);
    }

    *value = cpuState.wptr[cpuState.nWptr - index - 1];
  */
  addrWptrWord(index, &address);
  result = readBytes(address, 4, (unsigned long *)value);

  return (result);
}

/**************************
 */
int addrWptrWord(long index, long *address) {
  if (index >= cpuState.nWptr) {
    *address = 0;
    return (BAD_WPTR);
  }

  *address = (long)(wptrEndAddr + 1 - (cpuState.nWptr - index) * 4) & 0xFFFFFFFF;

  return (0);
}

/**************************
 */
int storeWptrWord(long index, long value) {
  int result = 0;
  long address;

  if (index >= cpuState.nWptr) {
    return (BAD_WPTR);
  }

  value &= 0xFFFFFFFF;

  addrWptrWord(index, &address);
  result = storeBytes(address, 4, value);
  /*  cpuState.wptr[cpuState.nWptr - index - 1] = value;*/
  cpuState.wptrUsed[cpuState.nWptr - index - 1] = TRUE;

  return (result);
}

int wptrPopState() {
  int result = 0;

  readWptrWord(0, &cpuState.iptr);

  if ((result = allocWptr((long)4))) {
    return (result);
  }

  return (result);
}

int wptrPushState() {
  int result = 0;

  if ((result = allocWptr((long)-4))) {
    return (result);
  }

  storeWptrWord(3, cpuState.creg);
  storeWptrWord(2, cpuState.breg);
  storeWptrWord(1, cpuState.areg);
  storeWptrWord(0, cpuState.iptr);

  return (result);
}

int allocWptr(long count) {
  int result = 0;
  long address;
  int i;

  /* allocate Wptr words */
  if (count < 0) {
    count *= -1;
    if (count + cpuState.nWptr > MAX_WPTR) {
      count = MAX_WPTR - cpuState.nWptr;
      result = WPTR_OVERFLOW;
    }
    for (i = 0; i < count; i++) {
      cpuState.nWptr++;
      addrWptrWord(0, &address);
      result = allocBytes(address, 4);
      /*		cpuState.wptr[cpuState.nWptr++] = UNDEFINED_WORD_OLD; */
    }
  }

  /* deallocate Wptr words */
  else if (count > 0) {
    if (count > cpuState.nWptr) {
      count = cpuState.nWptr;
      result = WPTR_UNDERFLOW;
    }
    for (i = 0; i < count; i++) {
      cpuState.wptrUsed[--cpuState.nWptr] = FALSE;
    }
  }

  return (result);
}

long pop(void) {
  long holdValue;

  holdValue = cpuState.areg;
  cpuState.areg = cpuState.breg;
  cpuState.breg = cpuState.creg;
  cpuState.creg = UNDEFINED_WORD_OLD;

  return (holdValue);
}

int push(long value) {
  cpuState.creg = cpuState.breg;
  cpuState.breg = cpuState.areg;
  cpuState.areg = value & 0xFFFFFFFF;

  return (0);
}

int setAreg(long value) {
  cpuState.areg = value & 0XFFFFFFFF;
  return (0);
}

int setBreg(long value) {
  cpuState.breg = value & 0XFFFFFFFF;
  return (0);
}

int setCreg(long value) {
  cpuState.creg = value & 0XFFFFFFFF;
  return (0);
}

int setIptr(long value) {
  cpuState.iptr = value & 0XFFFFFFFF;
  return (0);
}

char *st20Error(int error) {
  switch (error) {

  case BAD_WPTR:
    return ("Invalid Wptr word referenced");
    break;

  case WPTR_UNUSED:
    return ("An uninitialized Wptr word was accessed");
    break;

  case WPTR_UNDERFLOW:
    return ("Too many Wptr words deallocated");
    break;

  case WPTR_OVERFLOW:
    return ("Ran out of room for more Wptr words");
    break;

  case BAD_WATCH_CONDITION:
    return ("Parameters to the watch condition were invalid");
    break;

  default:
    return ("Unknown st20 error");
    break;
  }

  return (NULL);
}

int initTimer(FILE *outFp) {
  // init the timer fields:
  omrState.ClockRegHP = TIMER_GUESS;
  omrState.ClockRegLP = TIMER_GUESS;
  omrState.ClockEnables |= HPTIMER_MASK; // set bit0
  omrState.ClockEnables |= LPTIMER_MASK; // set bit1
  omrState.Enables = 0xffffc000;         // the initial state, see omr.h
                                         // bit 14 and 15 are reserved but set to 1
  //'ticking' code is into execInstr() subroutine
  fprintf(outFp, "CPU_CLOCK=%d Hz\n HPTimerTick=%ld cpucycles\n LPTimerTick=%ld cpucycles\n",
          CPU_CLOCK, hp_timertick, lp_timertick);

  return (0);
}
