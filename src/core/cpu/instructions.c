#include "st20.h"

#include "omr.h"

#include "../../common/defines.h"
// #include "../../soc/sti5518/STi5518_SearchDB.h"
#include "../memory/memory.h"

#include <stdint.h>
#include <stdio.h>

extern OMRSTATE omrState;
extern CPUSTATE cpuState;
extern int instrLength;
extern long wptrEndAddr;
extern long instrCode;
extern long st20ProductId;
extern long memStartVal;

extern long pop(void);
extern int push(long value);
extern int readWptrWord(long index, long *value);

extern INSTRENTRY instrEntry[];

/*
 * The instrCode is used as an index to find the instrCode mnemonic and the subroutine
 * that will emulate the instruction.
 */

int adc_(FILE *outFp, long operand) {
  /* warning... I'm not checking for overflow */
  push(pop() + operand);

  return (0);
}

int add_(FILE *outFp, long unused) {
  /* warning... I'm not checking for overflow */
  push(pop() + pop());

  return (0);
}

int ajw_(FILE *outFp, long value) {
  int result;

  result = allocWptr(value);
  if (result) {
    fprintf(outFp, "ERROR: %s\n", st20Error(result));
    return (-1);
  }
  return (0);
}

int and_(FILE *outFp, long unused) {
  push(pop() & pop());

  return (0);
}

int bcnt_(FILE *outFp, long unused) {
  push(pop() * 4);

  return (0);
}

/*
BITCNT (count bits set in word)
Code: 27 F6
Description: Count the number of bits set in Areg and add this to the value in Breg.
Definition:
  Areg' <- Breg + number of bits set to 1 in Areg
  Breg' <- Creg
  Creg' <- undefined
Error signals: none
*/
int bitcnt_(FILE *outFp, long unused) {
  unsigned long oldAreg = pop();
  unsigned long oldBreg = pop();
  int count = 0;

  while (oldAreg) {
    if (oldAreg & 0x8000)
      count++;
    oldAreg <<= 1;
  }

  push(oldBreg + count);

  return (0);
}

int bsub_(FILE *outFp, long unused) {
  push(pop() + pop());

  return (0);
}

int call_(FILE *outFp, long offset) {
  int result;
  unsigned long addr;
  addr = cpuState.iptr;
  result = wptrPushState();
  if (result) {
    fprintf(outFp, "ERROR: %s\n", st20Error(result));
    return (-1);
  }

  cpuState.areg = cpuState.iptr;
  cpuState.breg = UNDEFINED_WORD_OLD;
  cpuState.creg = UNDEFINED_WORD_OLD;
  cpuState.iptr = (cpuState.iptr + offset) & 0xFFFFFFFF;

  fprintf(outFp, "Call to %8lx ,return to %8lx\n", cpuState.iptr, addr);
  return (0);
}

int cj_(FILE *outFp, long offset) {
  if (cpuState.areg) { /* don't jump */
    pop();
  } else {
    cpuState.iptr = (cpuState.iptr + offset) & 0xFFFFFFFF;
  }

  return (0);
}

/*
NAME: devlb (device load byte)
Code: 2F F0
Description: Perform a device read from memory, a memory-mapped device or a
peripheral. The byte addressed by Areg is read into Areg as an unsigned value.
*/
int devlb_(FILE *outFp, long unused) {
  int result;
  long oldAreg, newAreg;
  unsigned long addr;

  // current address
  addr = get_iptr();

  oldAreg = pop();
  result = readBytes(oldAreg, 1, (unsigned long *)&newAreg);
  push(newAreg);

  // TODO: rework with commands.cpp when possible
  // if (needPrompt() || showRegs()) {
  fprintf(outFp, "NOTE: At 0x%08lx Read of device at address %08lx, value=0x%08lx\n",
          addr - instrLength, oldAreg, newAreg);
  fprintf(outFp, "Value of A register is questionable\n");

  // SearchForReg(outFp, oldAreg);
  // }

  if (result && result != READ_UNUSED_MEM) {
    fprintf(outFp, "ERROR: %s\n", memoryError(result));
    fprintf(outFp, "  Error occurred when executing devlb instruction\n");
    return (-1);
  }

  return (0);
}

/*
NAME: devls (device load sixteen)
Code: 2F F2
Description: Perform a device read from memory, a memory-mapped device or a
peripheral. The 16-bit object addressed by Areg is read into Areg as an unsigned
value.
*/
int devls_(FILE *outFp, long unused) {
  int result;
  long oldAreg, newAreg;
  unsigned long addr;

  // current address
  addr = get_iptr();

  oldAreg = pop();
  result = readBytes(oldAreg, 2, (unsigned long *)&newAreg);
  push(newAreg);

  // TODO: rework with commands.cpp when possible
  // if (needPrompt() || showRegs()) {
  fprintf(outFp, "NOTE: At 0x%08lx Read of device at address %08lx, value=0x%08lx\n",
          addr - instrLength, oldAreg, newAreg);
  fprintf(outFp, "Value of A register is questionable\n");

  // if (showRegs())
  //   SearchForReg(outFp, oldAreg);
  // }

  if (result && result != READ_UNUSED_MEM) {
    fprintf(outFp, "ERROR: %s\n", memoryError(result));
    fprintf(outFp, "  Error occurred when executing devls instruction\n");
    return (-1);
  }

  return (0);
}

/*
NAME: devlw (device load word)
Code: 2F F4
Description: Perform a device read from memory, a memory-mapped device or a
peripheral. The word addressed by Areg is read into Areg.
*/
int devlw_(FILE *outFp, long unused) {
  int result;
  long oldAreg, newAreg;
  unsigned long addr;

  // current address
  addr = get_iptr();

  oldAreg = pop();
  result = readBytes(oldAreg, 4, (unsigned long *)&newAreg);
  push(newAreg);

  // TODO: rework with commands.cpp when possible
  // if (needPrompt() || showRegs()) {
  fprintf(outFp, "NOTE: At 0x%08lx Read of device at address %08lx, value=0x%08lx\n",
          addr - instrLength, oldAreg, newAreg);
  fprintf(outFp, "Value of A register is questionable\n");

  //   if (showRegs())
  //     SearchForReg(outFp, oldAreg);
  // }

  if (result && result != READ_UNUSED_MEM) {
    fprintf(outFp, "ERROR: %s\n", memoryError(result));
    fprintf(outFp, "  Error occurred when executing devlw instruction\n");
    return (-1);
  }

  return (0);
}

/*
NAME: devsb (device store byte)
Code: 2F F1
Description: Perform a device write from memory, a memory-mapped device or a
peripheral. Store the least significant byte of Breg into the byte addressed by Areg.
*/
int devsb_(FILE *outFp, long value) {
  int result;
  long value1, value2;
  unsigned long addr;

  // current address
  addr = get_iptr();

  value1 = pop();
  value2 = pop();
  result = storeBytes(value1, 1, (unsigned char)(value2 & 0xFF));

  // TODO: rework with commands.cpp when possible
  // if (needPrompt() || showRegs()) {
  fprintf(outFp, "NOTE: At 0x%08lx Write to device at address %08lx, value=0x%08x\n",
          addr - instrLength, value1, (unsigned char)value2 & 0xFF);
  //   if (showRegs())
  //     SearchForReg(outFp, value1);
  // }

  if (result) {
    fprintf(outFp, "ERROR: %s\n", st20Error(result));
    fprintf(outFp, "  Error occurred when executing devsb instruction\n");
    return (-1);
  }

  return (0);
}

/*
NAME: devss (device store sixteen)
Code: 2F F3
Description: Perform a device write from memory, a memory-mapped device or a
peripheral. Store bits 0..5 of Breg into the sixteen bits addressed by Areg.
*/
int devss_(FILE *outFp, long value) {
  int result;
  long value1, value2;
  unsigned long addr;
  // current address
  addr = get_iptr();

  value1 = pop();
  value2 = pop();
  result = storeBytes(value1, 2, (unsigned int)(value2 & 0xFFFF));

  // TODO: rework with commands.cpp when possible
  // if (needPrompt() || showRegs()) {
  fprintf(outFp, "NOTE: At 0x%08lx Write to device at address %08lx, value=0x%08x\n",
          addr - instrLength, value1, (unsigned int)value2 & 0xFFFF);

  //   // Search description in register database
  //   if (showRegs())
  //     SearchForReg(outFp, value1);
  // }

  if (result) {
    fprintf(outFp, "ERROR: %s\n", st20Error(result));
    return (-1);
  }

  return (0);
}

/*
NAME: devsw (device store word)
Code: 2F F5
Description: Perform a device write from memory, a memory-mapped device or a
peripheral. Store Breg into the word of memory addressed by Areg.
*/
int devsw_(FILE *outFp, long value) {
  int result;
  long value1, value2;
  unsigned long addr;

  // current address
  addr = get_iptr();

  value1 = pop();
  value2 = pop();
  result = storeBytes(value1, 4, (unsigned long)(value2 & 0xFFFFFFFF));

  // TODO: rework with commands.cpp when possible
  // if (needPrompt() || showRegs()) {
  fprintf(outFp, "NOTE: At 0x%08lx Write to device at address %08lx, value=0x%08x\n",
          addr - instrLength, value1, (unsigned int)value2 & 0xFFFF);

  //   // Search description in register database
  //   if (showRegs())
  //     SearchForReg(outFp, value1);
  // }

  if (result) {
    fprintf(outFp, "ERROR: %s\n", st20Error(result));
    return (-1);
  }

  return (0);
}

/* Code: F4
  Description: Subtract Areg from Breg, without checking for overflow.
*/
int diff_(FILE *outFp, long unused) {
  uint32_t Areg, Breg;

  Areg = (uint32_t)pop();
  Breg = (uint32_t)pop();

  // We need to use uint to omit runtime integer overflow errors
  // It's safer than to depend on C undefined behaviors
  push((long)(int32_t)(Breg - Areg));

  return (0);
}
/* Code: 22 FC
  Description: Divide Breg by Areg, with checking for overflow. The result when not
  exact is rounded towards zero.
*/
int div_(FILE *outFp, long unused) {
  int32_t Areg, Breg;

  Areg = (int32_t)pop();
  Breg = (int32_t)pop();

  if ((Areg == 0) || ((Breg == -2147483648LL) && (Areg == -1))) {
    (void)pop(); // Areg' <- undefined
    // TODO: Signal IntegerOverflow per documentation
    return (0);
  }

  push((long)(Breg / Areg));

  return (0);
}

int dup_(FILE *outFp, long unused) {
  long value;

  value = pop();
  push(value);
  push(value);

  return (0);
}

int eqc_(FILE *outFp, long value) {
  long oldAreg;

  oldAreg = pop();
  if (oldAreg == (value & 0xFFFFFFFF)) {
    push((long)TRUE);
  } else {
    push((long)FALSE);
  }

  return (0);
}

int gajw_(FILE *outFp, long unused) {
  int result;
  long oldAreg;
  long newAreg;
  int i;
  long address;

  oldAreg = pop();
  if (oldAreg & 0x03) {
    fprintf(outFp, "WARNING: Value in A register does not point to a word boundary\n");
  }

  result = addrWptrWord(0, &newAreg);
  push(newAreg);
  if (result) {
    fprintf(outFp, "ERROR: %s\n", st20Error(result));
    return (-1);
  }
  wptrEndAddr = oldAreg + 3;
  cpuState.nWptr = 1;
  cpuState.wptrUsed[0] = TRUE;

  addrWptrWord((int)0, &address);
  result = allocBytes(address, 4);
  /*  cpuState.wptr[0] = UNDEFINED_WORD_OLD;*/

  for (i = 1; i < MAX_WPTR; i++) {
    cpuState.wptrUsed[i] = FALSE;
  }
  /*
    result = storeWptrWord (0, oldAreg);
    if (result) {
     fprintf (outFp, "ERROR: %s\n", st20Error(result));
     return (-1);
    }
  */
  return (0);
}

int gcall_(FILE *outFp, long unused) {
  long oldAreg;

  oldAreg = pop();
  push(cpuState.iptr);
  cpuState.iptr = oldAreg;

  return (0);
}

int gt_(FILE *outFp, long unused) {
  long value1, value2;

  value1 = pop();
  value2 = pop();

  push(value2 > value1);

  return (0);
}

int gtu_(FILE *outFp, long unused) {
  long value1, value2;

  value1 = pop();
  value2 = pop();

  push((unsigned long)value2 > (unsigned long)value1);

  return (0);
}

int invalidOp_(FILE *outFp, long unused) {
  fprintf(outFp, "This instruction (%s) has not been implemented yet\n",
          instrEntry[instrCode].mnemonic);
  return (-1);
}

/*	Code: Function 0
  Description: Unconditional relative jump. The destination of the jump is expressed as
  a byte offset from the first byte after the current instruction. j 0 causes a breakpoint.
*/
int j_(FILE *outFp, long offset) {
  pop();
  pop();
  pop();

  if (offset == 0) {
    fprintf(outFp, "Breakpoint signalled by jump instruction\n");
  } else {
    cpuState.iptr = (cpuState.iptr + offset) & 0xFFFFFFFF;
  }

  return (0);
}

/* Code: 21 F6
  Description: Add with carry in and check for overflow. The result of the operation is
  the sum of Areg, Breg and bit 0 of Creg.
*/
int ladd_(FILE *outFp, long unused) {
  int64_t Areg, Breg, Creg, sum;

  Areg = (int32_t)pop(); // Areg
  Breg = (int32_t)pop(); // Breg
  Creg = (int32_t)pop(); // Creg

  sum = Areg + Breg + (Creg & 0x01);

  if (sum > 0x7FFFFFFFLL) {
    push((long)(sum - 0x100000000LL));
    // TODO: Signal IntegerOverflow per documentation
  } else if (sum < -2147483648LL) {
    push((long)(sum + 0x100000000LL));
    // TODO: Signal IntegerOverflow per documentation
  } else {
    push((long)sum);
  }

  return (0);
}

/*	Code: F1
  Description: Load the unsigned byte addressed by Areg into Areg.
*/
int lb_(FILE *outFp, long unused) {
  int result;
  long oldAreg, newAreg;

  oldAreg = pop();
  result = readBytes(oldAreg, 1, (unsigned long *)&newAreg);
  push(newAreg);

  // TODO: rework with commands.cpp when possible
  // TODO: CPU module should not have a knowledge about CLI
  // if (needPrompt()) {
  fprintf(outFp, "NOTE: Read of memory address %08lx, value=0x%08lx\n", oldAreg, newAreg);
  // }

  if (result) {
    fprintf(outFp, "ERROR: %s\n", memoryError(result));
    fprintf(outFp, "  Error occurred when executing lb instruction\n");
    return (-1);
  }

  return (0);
}

/*	Code: 2B F9
  Description: Load the byte addressed by Areg into Areg and sign extend to a word.
*/
int lbx_(FILE *outFp, long unused) {
  int result;
  long oldAreg, newAreg;

  oldAreg = pop();
  result = readBytes(oldAreg, 1, (unsigned long *)&newAreg);
  if ((newAreg & 0x80) == 0x80) {
    newAreg = newAreg | 0xffffff00;
  }
  push(newAreg);

  // TODO: rework with commands.cpp when possible
  // if (needPrompt()) {
  fprintf(outFp, "NOTE: Read of memory address %08lx, value=0x%08lx\n", oldAreg, newAreg);
  // }

  if (result) {
    fprintf(outFp, "ERROR: %s\n", memoryError(result));
    fprintf(outFp, "  Error occurred when executing lbx instruction\n");
    return (-1);
  }

  return (0);
}

/*	Code: Function 4
  Description: Load constant into Areg.
*/
int ldc_(FILE *outFp, long operand) {
  push(operand);

  return (0);
}

/*	Code: 21 27 FC
  Description: See ldprodid. This instruction may be removed in future so ldprodid
  should be used instead.
*/
int lddevid_(FILE *outFp, long unused) {
  /*  fprintf (outFp, "Product ID is unknown.  Setting A register to %x\n",
          st20ProductId); */

  push((long)st20ProductId);

  return (0);
}

/*	Code: 24 FF
  Description: Subtract unsigned numbers with borrow in. Subtract Areg from Breg
  minus borrow in from Creg, producing difference in Areg and borrow out in Breg,
  without checking for overflow.
*/
int ldiff_(FILE *outFp, long unused) {
  unsigned long value1, value2, value3;
  unsigned long carry, temp;
  value1 = pop();
  value2 = pop();
  value3 = pop();
  carry = N_Add(value1, value3 & 0x01, &temp);
  value3 = value2 - temp;
  if ((temp > value2) || (carry == 1)) {
    push(1);
  } else {
    push(0);
  }
  push(value3);

  return (0);
}

int ldiv_(FILE *outFp, long operand) {
  unsigned long oldAreg, oldBreg, oldCreg;
  unsigned long bl, bh;
  unsigned long hval, lval, temp;
  unsigned long rem;

  oldAreg = pop();
  oldBreg = pop();
  oldCreg = pop();

  if (oldCreg >= oldAreg) {
    /* Overflow */
    return -1;
  }

  bl = oldBreg & MAX_INT;
  bh = oldBreg >> NBITS;

  temp = (oldCreg % oldAreg) * (1 << NBITS) + bh;

  hval = temp / oldAreg;

  temp = (temp % oldAreg) * (1 << NBITS) + bl;

  lval = temp / oldAreg;

  rem = temp % oldAreg;

  push(rem);
  push((hval << NBITS) + lval);

  return 0;
}

int ldl_(FILE *outFp, long index) {
  int result;
  long value;

  result = readWptrWord(index, &value);
  push(value);

  if (result) {
    fprintf(outFp, "ERROR: %s\n", st20Error(result));
    return (-1);
  }

  return (0);
}

int ldlp_(FILE *outFp, long index) {
  int result;
  long value;

  result = addrWptrWord(index, &value);
  push(value);

  if (result) {
    fprintf(outFp, "ERROR: %s\n", st20Error(result));
    return (-1);
  }

  return (0);
}

int ldmemstartval_(FILE *outFp, long unused) {
  push((long)memStartVal);
  return (0);
}

int ldnl_(FILE *outFp, long offset) {
  int result;
  uint32_t oldAreg;
  uint32_t address;
  unsigned long cWord = 0;

  oldAreg = (uint32_t)pop();

  if (oldAreg & 0x03) {
    fprintf(outFp, "WARNING: Attempt to access a word that is not on a word boundary\n");
  }

  address = oldAreg + (uint32_t)(offset * 4);

  result = readBytes((long)address, 4, &cWord);
  push((long)cWord);

  // TODO: rework with commands.cpp when possible
  // if (needPrompt()) {
  fprintf(outFp, "NOTE: Read of memory address %08x, value=0x%08lx\n", address, cWord);
  // }

  if (result) {
    fprintf(outFp, "ERROR: %s\n", memoryError(result));
    fprintf(outFp, "  Error occurred when executing ldnl %08x  iptr=%08lx\n", address,
            (long)get_iptr());
    return (-1);
  }

  return (0);
}

int ldnlp_(FILE *outFp, long offset) {
  push(pop() + offset * 4);
  return (0);
}

int ldpi_(FILE *outFp, long unused) {
  push(cpuState.iptr + pop());

  return (0);
}

int ldpri_(FILE *outFp, long unused) {
  fprintf(outFp, "Priority is unknown.  Setting A register to %x\n", LOW_PRIORITY);

  push((long)LOW_PRIORITY);

  return (0);
}

int ldprodid_(FILE *outFp, long unused) {
  /*  fprintf (outFp, "Product ID is unknown.  Setting A register to %x\n",
          st20ProductId); */

  push((long)st20ProductId);

  return (0);
}

/*
LDTIMER (load timer)
Code: 22 F2
Description: Load the value of the current priority timer into Areg.
Definition:
  Areg4 <- ClockReg[Priority]
  Breg4 <- Areg
  Creg4 <- Breg
Error signals: none
*/

// int ldtimer_ (FILE *outFp, long unused) {
//   push(timerGuess);
//   fprintf (outFp, "Read of timer.  Assuming timer value is %x\n", timerGuess);
//
//   return (0);
// }

int ldtraph_(FILE *outFp, long unused) {
  unsigned long oldAreg, oldBreg, oldCreg;
  unsigned long trapbase = 0x80000000;
  unsigned long value;
  // int result;
  // result was not used but could be for info
  oldAreg = pop();
  oldBreg = pop();
  oldCreg = pop();

  fprintf(outFp, "ldtraph: Group, &TrapHandler, priority: %lx, %lx,  %lx\n", oldAreg, oldBreg,
          oldCreg);

  trapbase = trapbase + 0x40 + 0x80 * oldCreg + 0x20 * oldAreg;

  // TODO: what happened here?
  // this reads 4 bytes from Breg and writes them into value variable

  // result =
  readBytes(oldBreg, 4, &value);

  // but
  // this reads 16 (sic!) bytes from Breg and writes them into trapbase
  // why 16 bytes now?
  // also this function does it with for loop 1 by 1 so there's performance issue
  // commented by: Omikorin

  // result =
  storeByteRange(oldBreg, trapbase, 0x10);
  return (0);
}

int lmul_(FILE *outFp, long operand) {
  unsigned long oldAreg, oldBreg, oldCreg;
  unsigned long bl, bh;
  unsigned long al, ah;
  unsigned long hval, lval, temp1, temp2, temp12;
  unsigned long carry;

  oldAreg = pop();
  oldBreg = pop();
  oldCreg = pop();

  al = oldAreg & 0xffff;
  ah = (oldAreg >> 16) & 0xffff;

  bl = oldBreg & 0xffff;
  bh = (oldBreg >> 16) & 0xffff;

  hval = ah * bh;
  lval = al * bl;
  temp1 = ah * bl;
  temp2 = al * bh;

  carry = N_Add(lval, oldCreg, &bl);
  hval = hval + carry;
  carry = N_Add(temp1, temp2, &temp12);
  hval = hval + (carry << 16);
  carry = N_Add((temp12 << 16) & 0xffff0000, bl, &al);
  hval = hval + carry;
  ah = ((temp12 >> 16) & 0xffff) + hval;

  push(ah);
  push(al);

  return 0;
}
// C= A+B with carry set if overflow
unsigned long N_Add(unsigned long A, unsigned long B, unsigned long *C) {
  int bit0_A, bit0_B;
  unsigned long t1, t2, t3, carry;
  t1 = A;
  t2 = B;
  t3 = (t1 << 31) >> 31;
  if (t3 == 0) {
    bit0_A = 0;
  } else {
    bit0_A = 1;
  }

  t3 = (t2 << 31) >> 31;
  if (t3 == 0) {
    bit0_B = 0;
  } else {
    bit0_B = 1;
  }

  t3 = (t1 >> 1) + (t2 >> 1);
  if ((bit0_A == 1) && (bit0_B == 1))
    t3++;
  if ((t3 >> 31) == 0) {
    carry = 0;
  } else {
    carry = 1;
  }
  *C = A + B;
  return (carry);
}

int ls_(FILE *outFp, long unused) {
  int result;
  long oldAreg, newAreg;

  oldAreg = pop();
  result = readBytes(oldAreg, 2, (unsigned long *)&newAreg);
  push(newAreg);

  // TODO: rework with commands.cpp when possible
  // if (needPrompt()) {
  fprintf(outFp, "NOTE: Read of memory address %08lx, value=0x%08lx\n", oldAreg, newAreg);
  // }

  if (result) {
    fprintf(outFp, "ERROR: %s\n", memoryError(result));
    fprintf(outFp, "  Error occurred when executing ls instruction\n");
    return (-1);
  }

  return (0);
}

int lshl_(FILE *outFp, long unused) {

  unsigned long oldBreg;
  unsigned long oldCreg;
  // double  value1;
  unsigned short oldAreg;

  oldAreg = (unsigned short)pop();
  oldBreg = pop();
  oldCreg = pop();

  if (oldAreg >= 32) {
    push((oldBreg << (oldAreg - 32)) & 0xffffffff);
    push(0);
  } else {
    push(((oldBreg >> (32 - oldAreg)) | (oldCreg << oldAreg)) & 0xffffffff);
    push((oldBreg << oldAreg) & 0xffffffff);
  }

  return (0);
}

int lshr_(FILE *outFp, long unused) {
  unsigned long oldBreg;
  unsigned long oldCreg;
  // double  value1;
  unsigned short oldAreg;

  oldAreg = (unsigned short)pop();
  oldBreg = pop();
  oldCreg = pop();
  if (oldAreg >= 32) {
    push(0);
    push((oldCreg >> (oldAreg - 32)) & 0xffffffff);
  } else {
    push(oldCreg >> oldAreg);
    push((oldCreg << (32 - oldAreg)) | (oldBreg >> oldAreg));
  }

  return (0);
}

/* Code: 23 F8
  Description: Subtract with borrow in and check for overflow. The result of the
  operation, put into Areg, is Breg minus Areg, minus bit 0 of Creg.
*/
int lsub_(FILE *outFp, long unused) {
  int64_t Areg, Breg, Creg, diff;

  Areg = (int32_t)pop();
  Breg = (int32_t)pop();
  Creg = (int32_t)pop();

  diff = Breg - Areg - (Creg & 0x01);

  if (diff > 0x7FFFFFFFLL) {
    push((long)(diff - 0x100000000LL));
    // TODO: Signal IntegerOverflow per documentation
  } else if (diff < -2147483648LL) {
    push((long)(diff + 0x100000000LL));
    // TODO: Signal IntegerOverflow per documentation
  } else {
    push((long)diff);
  }

  return (0);
}

int lsum_(FILE *outFp, long unused) {
  unsigned long value1, value2, value3;
  unsigned long carry, temp;

  value1 = pop();
  value2 = pop();
  value3 = pop();
  carry = N_Add(value1, (value3 & 0x01), &temp);
  carry = carry + N_Add(value2, temp, &value3);
  if (carry > 0) {
    push(1);
    push(value3 - 0xffffffff - 1);
  }

  else {
    push(0);
    push(value3);
  }

  return (0);
}

int mint_(FILE *outFp, long unused) {
  push(MINIMUM_INTEGER);

  return (0);
}

int move_(FILE *outFp, long unused) {
  int result;
  int oldAreg, oldBreg, oldCreg;

  oldAreg = pop();
  oldBreg = pop();
  oldCreg = pop();

  result = storeByteRange(oldCreg, oldBreg, (int)oldAreg);

  fprintf(outFp, "NOTE: Copy of %x bytes from address %08x to address %08x\n", oldAreg, oldCreg,
          oldBreg);

  if (result) {
    fprintf(outFp, "ERROR: %s\n", memoryError(result));
    fprintf(outFp, "  Error occurred when executing move instruction\n");
    return (-1);
  }

  return (0);
}

int mul_(FILE *outFp, long unused) {
  /* WARNING: no checking for overflow */
  push(pop() * pop());

  return (0);
}

int nop_(FILE *outFp, long unused) {
  return (0);
}

int not_(FILE *outFp, long unused) {
  push(~pop());

  return (0);
}

int or_(FILE *outFp, long unused) {
  push(pop() | pop());

  return (0);
}

int pop_(FILE *outFp, long unused) {
  pop();

  return (0);
}

int prod_(FILE *outFp, long unused) {
  push(pop() * pop());

  return (0);
}

int resetch_(FILE *outFp, long unused) {
  int result = 0;
  long oldAreg, newAreg;

  oldAreg = pop();

  if (oldAreg & 0x03) {
    fprintf(outFp, "WARNING: Attempt to access a word that is not on a word boundary\n");
  }

  result = readBytes(oldAreg, 4, (unsigned long *)&newAreg);
  push(newAreg);

  // TODO: rework with commands.cpp when possible
  // if (needPrompt()) {
  fprintf(outFp, "NOTE: Read of memory address %08lx, value=0x%08lx\n", oldAreg, newAreg);
  // }

  result = storeBytes(oldAreg, 4, (long)NOT_PROCESS);

  // TODO: rework with commands.cpp when possible
  // if (needPrompt()) {
  fprintf(outFp, "NOTE: Write memory address %08lx, value=0x%08x\n", oldAreg, MINIMUM_INTEGER);
  // }

  fprintf(outFp, "Channel at address %08lx was reset\n", oldAreg);

  return (result);
}

int ret_(FILE *outFp, long unused) {
  int result;

  result = wptrPopState();
  if (result) {
    fprintf(outFp, "ERROR: %s\n", st20Error(result));
    return (-1);
  }

  return (0);
}

int rev_(FILE *outFp, long unused) {
  int result = 0;
  long oldAreg, oldBreg;

  oldAreg = pop();
  oldBreg = pop();

  push(oldAreg);
  push(oldBreg);

  return (result);
}

int runp_(FILE *outFp, long unused) {
  int result = 0;
  long oldAreg;

  oldAreg = pop();
  pop();
  pop();

  fprintf(outFp, "A new process was started.  Process descriptor =0x%08lx\n", oldAreg);

  return (result);
}

int sb_(FILE *outFp, long value) {
  int result;
  long value1, value2;

  value1 = pop();
  value2 = pop();
  result = storeBytes(value1, 1, (unsigned char)(value2 & 0xFF));

  // TODO: rework with commands.cpp when possible
  // if (needPrompt()) {
  fprintf(outFp, "NOTE: Write to memory address %08lx, value=0x%08lx\n", value1, value2 & 0xFF);
  // }

  if (result) {
    fprintf(outFp, "ERROR: %s\n", st20Error(result));
    return (-1);
  }

  return (0);
}

int shl_(FILE *outFp, long unused) {
  long value;
  long nbits;

  nbits = pop();
  value = pop();
  push(value << nbits);

  return (0);
}

int shr_(FILE *outFp, long unused) {
  unsigned long value;
  long nbits;

  nbits = pop();
  value = pop();
  push(value >> nbits);

  return (0);
}

int signal_(FILE *outFp, long unused) {
  int result = 0;
  long oldAreg;

  oldAreg = pop();
  pop();
  pop();

  // TODO: rework with commands.cpp when possible
  // if (needPrompt()) {
  fprintf(outFp, "A signal was received for address 0x%08lx\n", oldAreg);
  // }

  return (result);
}

int ss_(FILE *outFp, long value) {
  int result;
  long value1, value2;

  value1 = pop();
  value2 = pop();
  result = storeBytes(value1, 2, (unsigned)(value2 & 0xFFFF));

  // TODO: rework with commands.cpp when possible
  // if (needPrompt()) {
  fprintf(outFp, "NOTE: Write to memory address %08lx, value=0x%08lx\n", value1, value2 & 0xFFFF);
  // }

  if (result) {
    fprintf(outFp, "ERROR: %s\n", st20Error(result));
    return (-1);
  }

  return (0);
}

int ssub_(FILE *outFp, long unused) {
  long oldAreg;
  long oldBreg;

  oldAreg = pop();
  oldBreg = pop();

  push(oldAreg + oldBreg * 2);

  return (0);
}

int startp_(FILE *outFp, long unused) {
  // int result;
  long oldAreg, oldBreg;

  oldAreg = pop();
  oldBreg = pop();
  pop();

  fprintf(outFp, "A new process was started.  Workspace=0x%08lx, Iptr=0x%08lx\n", oldAreg,
          cpuState.iptr + oldBreg);

  return (0);
}

/*
STCLOCK (store clock register)
Code: 64 FC
Description: Store the contents of Breg into the clock register of priority Areg.
Definition:
  ClockReg[Areg] <- Breg
  Areg' <- Creg
  Breg' <- undefined
  Creg' <- undefined
Error signals: none
*/

int stclock_(FILE *outFp, long unused) {
  long oldAreg;
  long oldBreg;

  oldAreg = pop();
  oldBreg = pop();

  fprintf(outFp, "0x%08lx stored in %s clock\n", oldBreg,
          (oldAreg & 1) ? "low priority" : "high priority");

  if (oldAreg & 1)
    omrState.ClockRegLP = oldBreg;
  else
    omrState.ClockRegHP = oldBreg;

  // omrState.Enables|=HPTIMER_MASK; // set bit0
  // omrState.Enables|=LPTIMER_MASK; // set bit1

  // AND NOW CLOCKS MUST START TICKING!!! But how can i do it?!
  // check 'Enables' flag....

  return (0);
}

int stl_(FILE *outFp, long index) {
  int result;

  result = storeWptrWord(index, pop());

  if (result) {
    fprintf(outFp, "ERROR: %s\n", st20Error(result));
    return (-1);
  }

  return (0);
}

/*
Code: Function 0x0E
Description: Store the contents of Breg into the non-local variable at the specified
       word offset from Areg.
*/
int stnl_(FILE *outFp, long offset) {
  int result;
  unsigned int oldAreg, oldBreg;

  oldAreg = pop();
  oldBreg = pop();

  result = storeBytes(oldAreg + offset * 4, 4, oldBreg);

  // TODO: rework with commands.cpp when possible
  // if (needPrompt()) {
  fprintf(outFp, "NOTE: Write to memory address %08lx, value=0x%08x\n", oldAreg + offset * 4,
          oldBreg);
  // }

  if (result) {
    fprintf(outFp, "ERROR: %s\n", st20Error(result));
    return (-1);
  }

  return (0);
}

int stopp_(FILE *outFp, long unused) {
  pop();
  pop();
  pop();

  fprintf(outFp, "This process has been terminated\n");

  return (0);
}

/*
STTIMER (store timer)
Code: 25 F4
Description: Initialize the timers. Set the low and high priority clock registers to the
value in Areg and start them ticking and scheduling ready processes.
Definition:
  Clockreg'[0] <- Areg
  Clockreg'[1] <- Areg
  Start timers

  Areg' <- Breg
  Breg' <- Creg
  Creg' <- undefined
Error signals: none
*/

// int sttimer_ (FILE *outFp, long unused) {
//   fprintf (outFp, "Low and high priority clock registers were set to 0x%08x\n",
//						pop());
//
//   return (0);
// }

int sub_(FILE *outFp, long unused) {
  long value1;
  long value2;

  value1 = pop();
  value2 = pop();

  /* warning... I'm not checking for overflow */
  push(value2 - value1);

  return (0);
}

int sum_(FILE *outFp, long unused) {
  push(pop() + pop());

  return (0);
}

int wait_(FILE *outFp, long unused) {
  long oldAreg;

  oldAreg = pop();
  pop();
  pop();

  // TODO: rework with commands.cpp when possible
  // if (needPrompt()) {
  fprintf(outFp, "Wait on semaphore at 0x%08lx\n", oldAreg);
  // }

  return (0);
}

int wcnt_(FILE *outFp, long unused) {
  // long value1, value2;
  long value1;

  value1 = pop();
  push(value1 & 0x3);
  push((value1 & 0xfffffffc) / 4);

  return (0);
}

int wsub_(FILE *outFp, long unused) {
  long value1, value2;

  value1 = pop();
  value2 = pop();

  push(value1 + value2 * 4);

  return (0);
}

int wsubdb_(FILE *outFp, long unused) {
  long value1, value2;

  value1 = pop();
  value2 = pop();

  push(value1 + value2 * 8);

  return (0);
}

int xdble_(FILE *outFp, long unused) {
  long value1;

  value1 = pop();
  if (value1 >= 0)
    push(0);
  else
    push(-1);

  push(value1);

  return (0);
}

int xor_(FILE *outFp, long unused) {
  push(pop() ^ pop());

  return (0);
}

int xsword_(FILE *outFp, long unused) {
  long value1;

  value1 = pop();
  if ((value1 & 0x8000) == 0x8000)
    value1 = value1 | 0xffff0000;

  push(value1);

  return (0);
}

/*
int _ (FILE *outFp, long value) {
  int result;

  if (result) {
   fprintf (outFp, "ERROR: %s\n", st20Error(result));
   return (-1);
  }

  return (0);
}
*/

//////////////////////////////////////////////////////////////////////////
//				TIMER RELATED FUNCTIONS
//////////////////////////////////////////////////////////////////////////

/*
LDTIMER (load timer)
Code: 22 F2
Description: Load the value of the current priority timer into Areg.
Definition:
  Areg' <- ClockReg[Priority]
  Breg' <- Areg
  Creg' <- Breg
Error signals: none
*/

int ldtimer_(FILE *outFp, long unused) {
  // Current Priority is obtained reading WDesc flag in the process descriptor
  // see #include OMR.h

  if (cpuState.nWptr & 0x01) { // HighPriority
    push(omrState.ClockRegHP);
    fprintf(outFp, "Read of HIGH_PRIORITY timer. Value is 0x%08lx\n", omrState.ClockRegHP);
  } else { // LowPriority
    push(omrState.ClockRegLP);
    fprintf(outFp, "Read of LOW_PRIORITY timer. Value is 0x%08lx\n", omrState.ClockRegLP);
  }
  //'ticking' code is into execInstr() subroutine
  return (0);
}

/*
STTIMER (store timer)
Code: 25 F4
Description: Initialize the timers. Set the low and high priority clock registers to the
value in Areg and start them ticking and scheduling ready processes.
Definition:
  Clockreg'[0] <- Areg
  Clockreg'[1] <- Areg
  Start timers

  Areg' <- Breg
  Breg' <- Creg
  Creg' <- undefined
Error signals: none
*/

int sttimer_(FILE *outFp, long unused) {

  omrState.ClockRegHP = omrState.ClockRegLP = pop();
  fprintf(outFp, "LOW and HIGH priority clock registers were set to 0x%08lx\n",
          omrState.ClockRegHP);

  omrState.ClockEnables |= HPTIMER_MASK; // set bit0
  omrState.ClockEnables |= LPTIMER_MASK; // set bit1

  //'ticking' code is into execInstr() subroutine

  return (0);
}

/*
CLOCKDIS (clock disable)
Code: 64 FE
Description: Stops the clocks specified in bits 0 and 1 of Areg where bit 0
indicates the high priority clock and bit 1 the low priority clock. The
original values of these two clock enable bits are returned in Areg.
Definition:
  Areg' 1..0 <- ClockEnables
  Areg' 31..2 <- 0
  ClockEnables' <- ClockEnables ^ ~Areg
Error signals: none
*/

int clockdis_(FILE *outFp, long unused) {

  long oldAreg = pop();
  long oldClockEnables = omrState.ClockEnables;

  if (oldAreg & HPTIMER_MASK)
    omrState.ClockEnables &= ~HPTIMER_MASK; // clear bit0
  if (oldAreg & LPTIMER_MASK)
    omrState.ClockEnables &= ~LPTIMER_MASK; // clear bit1

  push(oldClockEnables &= 0x03); // bit 0.1 leaved unchanged. others are zero'ed
                                 //'ticking' code is into execInstr() subroutine
  return (0);
}

/*
CLOCKENB (clock enable)
Code: 64 FF
Description: Starts or restarts the clocks specified in bits 0 and 1 of Areg,
where bit 0 indicates the high priority clock and bit 1 indicates the low
priority clock. The original values of these two clock enable bits are
returned in Areg.
Definition:
  Areg' 1..0 <- ClockEnables
  Areg' 31..2 <- 0
  ClockEnables' <- ClockEnables ! Areg
Error signals: none
*/

int clockenb_(FILE *outFp, long unused) {

  long oldAreg = pop();
  long oldClockEnables = omrState.ClockEnables;

  if (oldAreg & HPTIMER_MASK)
    omrState.ClockEnables |= HPTIMER_MASK; // set bit0
  if (oldAreg & LPTIMER_MASK)
    omrState.ClockEnables |= LPTIMER_MASK; // set bit1

  push(oldClockEnables &= 0x03); // bit 0.1 leaved unchanged. others are zero'ed
                                 //'ticking' code is into execInstr() subroutine
  return (0);
}

/*
LDCLOCK (load clock)
Code: 64 FD
Description: Load into Areg the current value of ClockReg, of the priority selected
by Areg, where 0 indicates high priority and 1 indicates low priority.
Definition:
  Areg' <- ClockReg[Areg]
Error signals: none
 */
int ldclock_(FILE *outFp, long unused) {
  long oldAreg;

  oldAreg = pop();

  fprintf(outFp, "Areg loaded with %s clock\n", (oldAreg & 1) ? "LOW_PRIORITY" : "HIGH_PRIORITY");

  if (oldAreg & 1)
    push(omrState.ClockRegLP);
  else
    push(omrState.ClockRegHP);

  //'ticking' code is into execInstr() subroutine

  return (0);
}

//////////////////////////////////////////////////////////////////////////
//				TRAP RELATED FUNCTIONS
//////////////////////////////////////////////////////////////////////////
/*
TRAPDIS (trap disable)
Code: 60 F6
Description: Disable those traps selected by the mask in Areg at the priority
       selected by Breg, where 0 indicates high priority and 1 indicates low priority.
       The original value of TrapEnables is returned in Areg.
Definition:
  TrapEnables'[Breg] <- TrapEnables[Breg] ^ ~Areg
  Areg'13..0 <- TrapEnables[Breg]
  Areg'31..14 <- 0
  Breg' <- undefined
  Creg' <- undefined
Error signals: none
*/
int trapdis_(FILE *outFp, long unused) {
  long Areg = pop();                  // traps mask
  long Breg = pop();                  // priority
  long oldEnables = omrState.Enables; // old Enables Status

  Areg &= 0x3fff; // consider only bit 0..13
  omrState.Enables &= ~Areg;
  push(oldEnables & 0x3fff); // return old status in Areg

  return (0);
}

/*
TRAPENB (trap enable)
Code: 60 F7
Description: Enable those traps selected by the mask in Areg at the priority selected
       by Breg, where 0 indicates high priority and 1 indicates low priority.
       The original value	of TrapEnables is returned in Areg.
Definition:
  TrapEnables'[Breg] <- TrapEnables[Breg] ! Areg
  Areg'13..0 <- TrapEnables[Breg]
  Areg'31..14 <- 0
  Breg' <- undefined
  Creg' <- undefined
Error signals: none
*/
int trapenb_(FILE *outFp, long unused) {
  long Areg = pop();                  // traps mask
  long Breg = pop();                  // priority
  long oldEnables = omrState.Enables; // old Enables Status

  Areg &= 0x3fff; // consider only bit 0..13
  omrState.Enables |= Areg;

  push(oldEnables & 0x3fff); // return old status in Areg

  return (0);
}

/*
GINTDIS (global interrupt disable)
Code: 2C FD
Description: Disable the global interrupt events specified in the bit mask in Areg.
This allows parts of the built-in scheduler, such as response to external events,
timeslicing etc., to be disabled by software. The original value of the global interrupt
enable register is returned in Areg.
Definition:
  GlobalInterruptEnables' <- GlobalInterruptEnables ^ ~Areg7..0
  Areg'7..0 <- GlobalInterruptEnables
  Areg'31..8 <- 0
Error signals: none
*/
int gintdis_(FILE *outFp, long unused) {
  long Areg = pop();                  // global intr events mask
  long oldEnables = omrState.Enables; // old Enables Status

  Areg &= 0xff; // CONSIDER ONLY BIT 7..0
  omrState.Enables &= ~Areg;

  oldEnables &= 0x3fc000; // consider only bit 16..23
  push(oldEnables << 16); // return old status in Areg

  return (0);
}

/*
GINTENB (global interrupt enable)
Code: 2C FE
Description: Enable the global interrupt events specified in the bit mask in Areg.
Definition:
  GlobalInterruptEnables' <- GlobalInterruptEnables ! Areg7..0
  Areg'7..0 <- GlobalInterruptEnables
  Areg'8..31 <- 0
Error signals: none
*/
int gintenb_(FILE *outFp, long unused) {
  long Areg = pop();                  // global intr events mask
  long oldEnables = omrState.Enables; // old Enables Status

  Areg &= 0xff; // CONSIDER ONLY BIT 7..0
  omrState.Enables |= Areg;

  oldEnables &= 0x3fc000; // consider only bit 16..23
  push(oldEnables << 16); // return old status in Areg

  return (0);
}
