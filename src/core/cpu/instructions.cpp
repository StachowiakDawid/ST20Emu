#include "instrentry.h"
#include "instructions.h"
#include "st20.h"
#include "omr.h"

#include "../../common/compat.h"
#include "../memory/memory.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <string_view>

extern OMRSTATE omrState;
extern CPUSTATE cpuState;
extern int instrLength;
extern uint32_t wptrEndAddr;
extern uint32_t instrCode;
extern uint32_t st20ProductId;
extern uint32_t memStartVal;

extern int readWptrWord(uint32_t index, uint32_t *value);

extern INSTRENTRY instrEntry[];

namespace {
int deviceAccess(int width, bool writeAccess, const char *mnemonic) {
  uint32_t address = pop();
  bool mappedDevice = isMemoryDeviceAddress(address);
  uint32_t value = 0;
  int result = 0;
  if (writeAccess) {
    uint32_t mask = width == BytesPerWord ? WordMask : (1U << (width * BitsPerByte)) - 1U;
    value = pop() & mask;
    result = storeBytes(address, width, value);
  } else {
    result = readBytes(address, width, &value);
    push(value);
  }

  compat::note("NOTE: At 0x{:08x} {} of device at address {:08x}, value=0x{:08x}",
                  get_iptr() - static_cast<uint32_t>(instrLength),
                  writeAccess ? "Write" : "Read", address, value);
  if (!writeAccess && !mappedDevice)
    compat::note("Value of A register is questionable");

  if (result && (mappedDevice || writeAccess || result != READ_UNUSED_MEM)) {
    compat::println("ERROR: {}", memoryError(result));
    compat::println("  Error occurred when executing {} instruction", mnemonic);
    return result;
  }
  return 0;
}

uint32_t reverseWord(uint32_t value) {
  value = ((value >> 1) & 0x55555555U) | ((value & 0x55555555U) << 1);
  value = ((value >> 2) & 0x33333333U) | ((value & 0x33333333U) << 2);
  value = ((value >> 4) & 0x0f0f0f0fU) | ((value & 0x0f0f0f0fU) << 4);
  value = ((value >> 8) & 0x00ff00ffU) | ((value & 0x00ff00ffU) << 8);
  return (value >> 16) | (value << 16);
}

int saveReadyQueue(unsigned char priority) {
  uint32_t address = cpuState.areg;
  int result = storeBytes(address, BytesPerWord, omrState.ProcQueueFPtr[priority]);
  if (!result) result = storeBytes(address + BytesPerWord, BytesPerWord, omrState.ProcQueueBPtr[priority]);
  if (!result) pop();
  return result;
}

int crcSteps(uint32_t count) {
  uint32_t data = pop();
  uint32_t checksum = pop();
  uint32_t polynomial = cpuState.areg;
  for (uint32_t bit = 0; bit < count; ++bit) {
    bool outgoing = (checksum & WordSignBit) != 0;
    checksum = (checksum << 1) | ((data >> (BitsPerWord - 1 - bit)) & 1U);
    if (outgoing) checksum ^= polynomial;
  }
  push(checksum);
  return 0;
}

uint32_t saturateWord(int64_t value) {
  return static_cast<uint32_t>(std::clamp<int64_t>(value,
      std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
}

uint64_t roundEvenShift(uint64_t value, int64_t shift) {
  if (shift > BitsPerDoubleWord) return 0;
  if (shift == BitsPerDoubleWord) return value > (uint64_t{1} << 63) ? 1 : 0;
  uint64_t quotient = value >> shift;
  uint64_t remainder = value & ((uint64_t{1} << shift) - 1);
  uint64_t half = uint64_t{1} << (shift - 1);
  return quotient + (remainder > half || (remainder == half && (quotient & 1U)) ? 1U : 0U);
}

enum class Move2dFilter { All, Nonzero, Zero };

int copy2d(Move2dFilter filter) {
  uint32_t width = pop();
  uint32_t destination = pop();
  uint32_t source = pop();
  uint32_t rows = cpuState.move2dBlockLength;
  uint32_t sourceStride = cpuState.move2dSourceStride;
  uint32_t destinationStride = cpuState.move2dDestStride;
  if (width == 0 || rows == 0) return 0;
  for (uint32_t row = 0; row < rows; ++row) {
    uint32_t sourceRow = source + row * sourceStride;
    uint32_t destinationRow = destination + row * destinationStride;
    if (filter == Move2dFilter::All) {
      int result = storeByteRange(sourceRow, destinationRow, width);
      if (result) return result;
      continue;
    }
    auto shouldCopy = [filter](uint32_t value) {
      return filter == Move2dFilter::Nonzero ? value != 0 : value == 0;
    };
    auto copyByte = [&](uint32_t column) {
      uint32_t value = 0;
      int result = readBytes(sourceRow + column, 1, &value);
      if (!result && shouldCopy(value)) {
        result = storeBytes(destinationRow + column, 1, value);
      }
      return result;
    };
    auto copyWord = [&](uint32_t column) {
      uint32_t value = 0;
      int result = readBytes(sourceRow + column, BytesPerWord, &value);
      if (result) return result;

      bool hasZeroByte = false;
      for (uint32_t byte = 0; byte < BytesPerWord; ++byte)
        hasZeroByte |= ((value >> (byte * BitsPerByte)) & 0xffU) == 0;

      if ((filter == Move2dFilter::Nonzero && !hasZeroByte) ||
          (filter == Move2dFilter::Zero && value == 0)) {
        return storeBytes(destinationRow + column, BytesPerWord, value);
      }
      if ((filter == Move2dFilter::Nonzero && value == 0) ||
          (filter == Move2dFilter::Zero && !hasZeroByte))
        return 0;

      for (uint32_t byte = 0; byte < BytesPerWord; ++byte) {
        result = copyByte(column + byte);
        if (result) return result;
      }
      return 0;
    };

    uint32_t column = 0;
    uint32_t sourcePrefix = (BytesPerWord - (sourceRow & 0xfU)) & 0xfU;
    uint32_t destinationPrefix = (BytesPerWord - (destinationRow & 0xfU)) & 0xfU;
    if (sourcePrefix == destinationPrefix) {
      uint32_t prefixLength = std::min(width, sourcePrefix);
      for (; column < prefixLength; ++column) {
        int result = copyByte(column);
        if (result) return result;
      }
      uint32_t wordLength = (width - column) & ~(BytesPerWord - 1U);
      for (uint32_t offset = 0; offset < wordLength; offset += BytesPerWord) {
        int result = copyWord(column + offset);
        if (result) return result;
      }
      column += wordLength;
    }
    for (; column < width; ++column) {
      int result = copyByte(column);
      if (result) return result;
    }
  }
  return 0;
}

}

/*
 * The instrCode is used as an index to find the instrCode mnemonic and the subroutine
 * that will emulate the instruction.
 */

int saveh_(uint32_t /*unused*/) {
  return saveReadyQueue(HIGH_PRIORITY);
}

int savel_(uint32_t /*unused*/) {
  return saveReadyQueue(LOW_PRIORITY);
}

int sthb_(uint32_t /*unused*/) {
  return setReadyQueue(HIGH_PRIORITY, omrState.ProcQueueFPtr[HIGH_PRIORITY], pop());
}

int sthf_(uint32_t /*unused*/) {
  return setReadyQueue(HIGH_PRIORITY, pop(), omrState.ProcQueueBPtr[HIGH_PRIORITY]);
}

int stlb_(uint32_t /*unused*/) {
  return setReadyQueue(LOW_PRIORITY, omrState.ProcQueueFPtr[LOW_PRIORITY], pop());
}

int stlf_(uint32_t /*unused*/) {
  return setReadyQueue(LOW_PRIORITY, pop(), omrState.ProcQueueBPtr[LOW_PRIORITY]);
}

int swapqueue_(uint32_t /*unused*/) {
  uint32_t priority = cpuState.areg;
  if (priority > LOW_PRIORITY) return INVALID_PROCESS_LIST;
  uint32_t front = omrState.ProcQueueFPtr[priority];
  uint32_t back = omrState.ProcQueueBPtr[priority];
  int result = setReadyQueue(priority, cpuState.breg, cpuState.creg);
  if (result) return result;
  cpuState.areg = front;
  cpuState.breg = back;
  cpuState.creg = undefinedWord;
  return 0;
}

int insertqueue_(uint32_t /*unused*/) {
  int result = insertReadyQueue(cpuState.areg,
                               cpuState.breg, cpuState.creg);
  if (result) return result;
  pop();
  pop();
  pop();
  return 0;
}

int endp_(uint32_t /*unused*/) {
  return endParallel(cpuState.areg);
}

int fmul_(uint32_t /*unused*/) {
  int64_t oldAreg = static_cast<int32_t>(pop());
  int64_t oldBreg = static_cast<int32_t>(pop());
  if (oldAreg == INT32_MIN && oldBreg == INT32_MIN) {
    push(undefinedWord);
    return signalArithmeticError(INTEGER_OVF_TRAPENB);
  }
  int64_t product = oldAreg * oldBreg;
  uint64_t magnitude = static_cast<uint64_t>(product < 0 ? -product : product);
  uint64_t rounded = magnitude >> 31;
  uint64_t remainder = magnitude & 0x7fffffffU;
  if (remainder > 0x40000000U || (remainder == 0x40000000U && (rounded & 1U))) ++rounded;
  int64_t result = product < 0 ? -static_cast<int64_t>(rounded) : static_cast<int64_t>(rounded);
  push(result);
  return 0;
}

int fptesterr_(uint32_t /*unused*/) {
  push(1);
  return 0;
}

int unpacksn_(uint32_t /*unused*/) {
  uint32_t packed = cpuState.areg;
  uint32_t oldBreg = cpuState.breg;
  uint32_t fraction = packed & 0x007fffffU;
  uint32_t exponent = (packed >> 23) & 0xffU;
  uint32_t type = exponent == 0xffU ? (fraction == 0 ? 2U : 3U)
                                 : (exponent == 0 && fraction == 0 ? 0U : 1U);
  cpuState.areg = exponent == 0xffU ? undefinedWord
      : (fraction << 8) | (exponent != 0 ? WordSignBit : 0U);
  cpuState.breg = exponent == 0 && fraction != 0 ? 1U : exponent;
  cpuState.creg = (oldBreg << 2) + type;
  return 0;
}

int testpranal_(uint32_t /*unused*/) {
  push(omrState.analysedOnReset ? 1 : 0);
  return 0;
}

int postnormsn_(uint32_t /*unused*/) {
  uint32_t count = cpuState.creg;
  uint32_t temporary = 0;
  int result = readWptrWord(pw::Temp, &temporary);
  if (result) return result;
  int64_t exponent = static_cast<int32_t>(temporary - count);
  if (exponent < -23) {
    cpuState.areg = 0;
    cpuState.breg = 0;
    exponent = 0;
  } else if (exponent <= 0) {
    uint64_t significand = (static_cast<uint64_t>(cpuState.breg) << BitsPerWord) | cpuState.areg;
    int64_t shift = 1 - exponent;
    significand = (significand >> shift) | cpuState.areg;
    cpuState.areg = static_cast<uint32_t>(significand);
    cpuState.breg = static_cast<uint32_t>(significand >> BitsPerWord);
    exponent = 0;
  }
  cpuState.creg = static_cast<uint32_t>(std::min<int64_t>(exponent, 255));
  return 0;
}

int roundsn_(uint32_t /*unused*/) {
  uint64_t significand = (static_cast<uint64_t>(cpuState.breg & 0x7fffffffU) << BitsPerWord) | cpuState.areg;
  uint64_t rounded = roundEvenShift(significand, 40);
  uint32_t exponent = cpuState.creg;
  uint32_t packed = static_cast<int32_t>(exponent) >= 255 ? 0x7f800000U
      : (exponent << 23) + static_cast<uint32_t>(rounded);
  cpuState.areg = packed;
  cpuState.breg = undefinedWord;
  cpuState.creg = undefinedWord;
  return 0;
}

int restart_(uint32_t /*unused*/) {
  return restartProcess(cpuState.areg);
}

int ldshadow_(uint32_t /*unused*/) {
  int result = transferShadowRegisters(cpuState.areg,
      cpuState.breg, cpuState.creg, true);
  if (result) return result;
  pop();
  pop();
  pop();
  return 0;
}

int stshadow_(uint32_t /*unused*/) {
  int result = transferShadowRegisters(cpuState.areg,
      cpuState.breg, cpuState.creg, false);
  if (result) return result;
  pop();
  pop();
  pop();
  return 0;
}

int iret_(uint32_t /*unused*/) {
  return returnFromInterrupt();
}

int intdis_(uint32_t /*unused*/) {
  if (cpuState.priority == LOW_PRIORITY) cpuState.interruptsDisabled = true;
  return 0;
}

int intenb_(uint32_t /*unused*/) {
  if (cpuState.priority == LOW_PRIORITY) cpuState.interruptsDisabled = false;
  return 0;
}

int settimeslice_(uint32_t /*unused*/) {
  if (cpuState.priority != LOW_PRIORITY) return 0;
  bool previous = (cpuState.status & STATUS_TIMESLICE_ENABLE) != 0;
  if (cpuState.areg == 0) cpuState.status &= ~STATUS_TIMESLICE_ENABLE;
  else cpuState.status |= STATUS_TIMESLICE_ENABLE;
  cpuState.areg = previous ? 1 : 0;
  return 0;
}

int timeslice_(uint32_t /*unused*/) {
  return timesliceProcess();
}

int cb_(uint32_t /*unused*/) {
  int32_t value = static_cast<int32_t>(cpuState.areg);
  return value < -128 || value > 127 ? signalArithmeticError(INTEGER_ERR_TRAPENB) : 0;
}

int cbu_(uint32_t /*unused*/) {
  return cpuState.areg > 255 ? signalArithmeticError(INTEGER_ERR_TRAPENB) : 0;
}

int cs_(uint32_t /*unused*/) {
  int32_t value = static_cast<int32_t>(cpuState.areg);
  return value < -32768 || value > 32767 ? signalArithmeticError(INTEGER_ERR_TRAPENB) : 0;
}

int csu_(uint32_t /*unused*/) {
  return cpuState.areg > 65535 ? signalArithmeticError(INTEGER_ERR_TRAPENB) : 0;
}

int csngl_(uint32_t /*unused*/) {
  uint32_t sign = static_cast<int32_t>(cpuState.areg) < 0 ? WordMask : 0;
  uint32_t high = cpuState.breg;
  cpuState.breg = cpuState.creg;
  cpuState.creg = undefinedWord;
  return high != sign ? signalArithmeticError(INTEGER_ERR_TRAPENB) : 0;
}

int cflerr_(uint32_t /*unused*/) {
  return (cpuState.areg & 0x7f800000U) == 0x7f800000U
      ? signalArithmeticError(INTEGER_ERR_TRAPENB) : 0;
}

int ccnt1_(uint32_t /*unused*/) {
  uint32_t bound = pop();
  uint32_t value = cpuState.areg;
  return value == 0 || value > bound ? signalArithmeticError(INTEGER_ERR_TRAPENB) : 0;
}

int csub0_(uint32_t /*unused*/) {
  uint32_t bound = pop();
  return cpuState.areg >= bound ? signalArithmeticError(INTEGER_ERR_TRAPENB) : 0;
}

int cword_(uint32_t /*unused*/) {
  int64_t bound = pop();
  int64_t value = static_cast<int32_t>(cpuState.areg);
  return value < -bound || value >= bound ? signalArithmeticError(INTEGER_ERR_TRAPENB) : 0;
}

int cir_(uint32_t /*unused*/) {
  int32_t lower = static_cast<int32_t>(pop());
  int32_t upper = static_cast<int32_t>(pop());
  int32_t value = static_cast<int32_t>(cpuState.areg);
  return value < lower || value > upper ? signalArithmeticError(INTEGER_ERR_TRAPENB) : 0;
}

int ciru_(uint32_t /*unused*/) {
  uint32_t lower = pop();
  uint32_t upper = pop();
  uint32_t value = cpuState.areg;
  return value < lower || value > upper ? signalArithmeticError(INTEGER_ERR_TRAPENB) : 0;
}

int adc_(uint32_t operand) {
  int64_t value = static_cast<int32_t>(pop());
  int64_t sum = value + static_cast<int32_t>(operand);
  push(sum);
  return sum > INT32_MAX || sum < INT32_MIN ? signalArithmeticError(INTEGER_OVF_TRAPENB) : 0;
}

int add_(uint32_t /*unused*/) {
  int64_t oldAreg = static_cast<int32_t>(pop());
  int64_t oldBreg = static_cast<int32_t>(pop());
  int64_t sum = oldBreg + oldAreg;
  push(sum);
  return sum > INT32_MAX || sum < INT32_MIN ? signalArithmeticError(INTEGER_OVF_TRAPENB) : 0;
}

int ajw_(uint32_t value) {
  int result;

  result = allocWptr(value);
  if (result) {
    compat::println("ERROR: {}", st20Error(result));
    return -1;
  }
  return 0;
}

int alt_(uint32_t /*unused*/) {
  return beginAlternative(false);
}

int talt_(uint32_t /*unused*/) {
  return beginAlternative(true);
}

int altwt_(uint32_t /*unused*/) {
  pop();
  pop();
  pop();
  return waitAlternative();
}

int altend_(uint32_t /*unused*/) {
  return finishAlternative();
}

int and_(uint32_t /*unused*/) {
  push(pop() & pop());

  return 0;
}

int bcnt_(uint32_t /*unused*/) {
  push(pop() * BytesPerWord);

  return 0;
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
int bitcnt_(uint32_t /*unused*/) {
  uint32_t oldAreg = pop();
  uint32_t oldBreg = pop();
  uint32_t count = static_cast<uint32_t>(std::popcount(oldAreg));
  push(oldBreg + count);

  return 0;
}

int bitrevword_(uint32_t /*unused*/) {
  push(reverseWord(pop()));
  return 0;
}

int bitrevnbits_(uint32_t /*unused*/) {
  uint32_t count = pop();
  uint32_t value = pop();
  uint32_t reversed = undefinedWord;
  if (count == 0)
    reversed = 0;
  else if (count <= BitsPerWord)
    reversed = reverseWord(value) >> (BitsPerWord - count);
  push(reversed);
  return 0;
}

int bsub_(uint32_t /*unused*/) {
  push(pop() + pop());

  return 0;
}

int call_(uint32_t offset) {
  uint32_t addr = cpuState.iptr;

  int result = wptrPushState();
  if (result != 0) {
    compat::println("ERROR: {}", st20Error(result));
    return -1;
  }

  cpuState.areg = cpuState.iptr;
  cpuState.breg = undefinedWord;
  cpuState.creg = undefinedWord;
  cpuState.iptr += offset;

  compat::note("Call to {:08x} ,return to {:08x}", cpuState.iptr, addr);

  return 0;
}

int cj_(uint32_t offset) {
  if (cpuState.areg) {
    pop();
  } else {
    cpuState.iptr = cpuState.iptr + offset;
  }

  return 0;
}

int causeerror_(uint32_t /*unused*/) {
  uint32_t cause = cpuState.areg;
  unsigned char priority = cause >= INTERNALCH_TRAPENB
                               ? static_cast<unsigned char>(cpuState.breg & 1U)
                               : cpuState.priority;
  return requestTrap(cause, priority, true);
}

int clrhalterr_(uint32_t /*unused*/) {
  omrState.HaltOnErrorFlag = 0;
  return 0;
}

int crcbyte_(uint32_t /*unused*/) {
  return crcSteps(BitsPerByte);
}

int crcword_(uint32_t /*unused*/) {
  return crcSteps(BitsPerWord);
}

/*
NAME: devlb (device load byte)
Code: 2F F0
Description: Perform a device read from memory, a memory-mapped device or a
peripheral. The byte addressed by Areg is read into Areg as an unsigned value.
*/
int devlb_(uint32_t /*unused*/) {
  return deviceAccess(1, false, "devlb");
}

/*
NAME: devls (device load sixteen)
Code: 2F F2
Description: Perform a device read from memory, a memory-mapped device or a
peripheral. The 16-bit object addressed by Areg is read into Areg as an unsigned
value.
*/
int devls_(uint32_t /*unused*/) {
  return deviceAccess(2, false, "devls");
}

/*
NAME: devlw (device load word)
Code: 2F F4
Description: Perform a device read from memory, a memory-mapped device or a
peripheral. The word addressed by Areg is read into Areg.
*/
int devlw_(uint32_t /*unused*/) {
  return deviceAccess(BytesPerWord, false, "devlw");
}

/*
NAME: devsb (device store byte)
Code: 2F F1
Description: Perform a device write from memory, a memory-mapped device or a
peripheral. Store the least significant byte of Breg into the byte addressed by Areg.
*/
int devsb_(uint32_t /*value*/) {
  return deviceAccess(1, true, "devsb");
}

/*
NAME: devss (device store sixteen)
Code: 2F F3
Description: Perform a device write from memory, a memory-mapped device or a
peripheral. Store bits 0..5 of Breg into the sixteen bits addressed by Areg.
*/
int devss_(uint32_t /*value*/) {
  return deviceAccess(2, true, "devss");
}

/*
NAME: devsw (device store word)
Code: 2F F5
Description: Perform a device write from memory, a memory-mapped device or a
peripheral. Store Breg into the word of memory addressed by Areg.
*/
int devsw_(uint32_t /*value*/) {
  return deviceAccess(BytesPerWord, true, "devsw");
}

/* Code: F4
  Description: Subtract Areg from Breg, without checking for overflow.
*/
int diff_(uint32_t /*unused*/) {
  uint32_t Areg = pop();
  uint32_t Breg = pop();

  push(Breg - Areg);

  return 0;
}

int disc_(uint32_t /*unused*/) {
  uint32_t offset = pop();
  bool enabled = pop() != 0;
  uint32_t channel = pop();
  uint32_t selected = 0;
  int result = disableAltChannel(offset, enabled, channel, &selected);
  push(selected);
  return result;
}

int diss_(uint32_t /*unused*/) {
  uint32_t offset = pop();
  bool enabled = pop() != 0;
  uint32_t selected = 0;
  int result = disableAltSkip(offset, enabled, &selected);
  push(selected);
  return result;
}

int dist_(uint32_t /*unused*/) {
  uint32_t offset = pop();
  bool enabled = pop() != 0;
  uint32_t time = pop();
  uint32_t selected = 0;
  int result = disableAltTimer(offset, enabled, time, &selected);
  push(selected);
  return result;
}

int div_(uint32_t /*unused*/) {
  int32_t Areg = static_cast<int32_t>(pop());
  int32_t Breg = static_cast<int32_t>(pop());

  if ((Areg == 0) || ((Breg == MostNeg) && (Areg == -1))) {
    push(undefinedWord);
    return signalArithmeticError(INTEGER_OVF_TRAPENB);
  }

  push(Breg / Areg);

  return 0;
}

int dup_(uint32_t /*unused*/) {
  uint32_t value = pop();
  push(value);
  push(value);

  return 0;
}

int enbc_(uint32_t /*unused*/) {
  uint32_t guard = pop();
  uint32_t channel = pop();
  int result = enableAltChannel(guard != 0, channel);
  push(guard);
  return result;
}

int enbt_(uint32_t /*unused*/) {
  uint32_t guard = pop();
  uint32_t time = pop();
  int result = enableAltTimer(guard != 0, time);
  push(guard);
  return result;
}

int enbs_(uint32_t /*unused*/) {
  return enableAltSkip(cpuState.areg != 0);
}

int eqc_(uint32_t value) {
  uint32_t oldAreg = pop();

  if (oldAreg == value) {
    push(1U);
  } else {
    push(0U);
  }

  return 0;
}

int gajw_(uint32_t /*unused*/) {
  uint32_t address = cpuState.areg;
  cpuState.areg = cpuState.wptr;
  setWptr(address);
  return 0;
}

int gcall_(uint32_t /*unused*/) {
  uint32_t oldAreg;

  oldAreg = pop();
  push(cpuState.iptr);
  cpuState.iptr = oldAreg;

  return 0;
}

int gt_(uint32_t /*unused*/) {
  int32_t oldAreg = static_cast<int32_t>(pop());
  int32_t oldBreg = static_cast<int32_t>(pop());
  push(oldBreg > oldAreg);

  return 0;
}

int gtu_(uint32_t /*unused*/) {
  uint32_t value1 = pop();
  uint32_t value2 = pop();

  push(value2 > value1);

  return 0;
}

int in_(uint32_t /*unused*/) {
  uint32_t length = pop();
  uint32_t channel = pop();
  uint32_t buffer = pop();
  return transferChannel(channel, length, buffer, true);
}

int invalidOp_(uint32_t /*unused*/) {
  if (std::string_view(instrEntry[instrCode].mnemonic) == "invalidop" &&
      (trapEnableMask(cpuState.priority) & ILL_OPCODE_TRAPENB))
    return requestTrap(ILL_OPCODE_TRAPENB, cpuState.priority);
  compat::println("This instruction ({}) has not been implemented yet",
                  instrEntry[instrCode].mnemonic);
  return -1;
}

/*	Code: Function 0
  Description: Unconditional relative jump. The destination of the jump is expressed as
  a byte offset from the first byte after the current instruction. j 0 causes a breakpoint.
*/
int j_(uint32_t offset) {
  pop();
  pop();
  pop();

  if (offset == 0) {
    int result = requestTrap(BREAKPOINT_TRAPENB, cpuState.priority);
    if (result) return result;
  } else {
    cpuState.iptr += offset;
  }

  return checkPeriodicTimeslice();
}

/* Code: 21 F6
  Description: Add with carry in and check for overflow. The result of the operation is
  the sum of Areg, Breg and bit 0 of Creg.
*/
int ladd_(uint32_t /*unused*/) {
  int64_t Areg, Breg, Creg, sum;

  Areg = static_cast<int32_t>(pop()); // Areg
  Breg = static_cast<int32_t>(pop()); // Breg
  Creg = static_cast<int32_t>(pop()); // Creg

  sum = Areg + Breg + (Creg & 0x01);

  if (sum > MostPos) {
    push(sum - (int64_t{1} << BitsPerWord));
    return signalArithmeticError(INTEGER_OVF_TRAPENB);
  } else if (sum < MostNeg) {
    push(sum + (int64_t{1} << BitsPerWord));
    return signalArithmeticError(INTEGER_OVF_TRAPENB);
  } else {
    push(sum);
  }

  return 0;
}

/*	Code: F1
  Description: Load the unsigned byte addressed by Areg into Areg.
*/
int lb_(uint32_t /*unused*/) {
  int result;
  uint32_t oldAreg, newAreg;

  oldAreg = pop();
  result = readBytes(oldAreg, 1, &newAreg);
  push(newAreg);

  compat::note("NOTE: Read of memory address {:08x}, value=0x{:08x}", oldAreg, newAreg);

  if (result) {
    compat::println("ERROR: {}", memoryError(result));
    compat::println("  Error occurred when executing lb instruction");
    return -1;
  }

  return 0;
}

/*	Code: 2B F9
  Description: Load the byte addressed by Areg into Areg and sign extend to a word.
*/
int lbx_(uint32_t /*unused*/) {
  int result;
  uint32_t oldAreg, newAreg;

  oldAreg = pop();
  result = readBytes(oldAreg, 1, &newAreg);
  if ((newAreg & 0x80) == 0x80) {
    newAreg = newAreg | 0xffffff00;
  }
  push(newAreg);

  compat::note("NOTE: Read of memory address {:08x}, value=0x{:08x}", oldAreg, newAreg);

  if (result) {
    compat::println("ERROR: {}", memoryError(result));
    compat::println("  Error occurred when executing lbx instruction");
    return -1;
  }

  return 0;
}

/*	Code: Function 4
  Description: Load constant into Areg.
*/
int ldc_(uint32_t operand) {
  push(operand);

  return 0;
}

/*	Code: 21 27 FC
  Description: See ldprodid. This instruction may be removed in future so ldprodid
  should be used instead.
*/
int lddevid_(uint32_t /*unused*/) {
  push(st20ProductId);

  return 0;
}

/*	Code: 24 FF
  Description: Subtract unsigned numbers with borrow in. Subtract Areg from Breg
  minus borrow in from Creg, producing difference in Areg and borrow out in Breg,
  without checking for overflow.
*/
int ldiff_(uint32_t /*unused*/) {
  uint32_t temp;

  uint32_t value1 = pop();
  uint32_t value2 = pop();
  uint32_t value3 = pop();

  uint32_t carry = N_Add(value1, value3 & 0x01, &temp);
  value3 = value2 - temp;

  if ((temp > value2) || (carry == 1)) {
    push(1U);
  } else {
    push(0U);
  }

  push(value3);

  return 0;
}

int ldiv_(uint32_t /*operand*/) {
  uint32_t oldAreg = cpuState.areg;
  uint32_t oldBreg = cpuState.breg;
  uint32_t oldCreg = cpuState.creg;

  if (oldCreg >= oldAreg) {
    cpuState.creg = undefinedWord;
    return signalArithmeticError(INTEGER_OVF_TRAPENB);
  }
  pop();
  pop();
  pop();
  uint64_t dividend = (static_cast<uint64_t>(oldCreg) << BitsPerWord) | oldBreg;
  push(dividend % oldAreg);
  push(dividend / oldAreg);

  return 0;
}

int ldinf_(uint32_t /*unused*/) {
  push(0x7f800000U);
  return 0;
}

int lend_(uint32_t /*unused*/) {
  uint32_t offset = pop();
  uint32_t address = pop();
  pop();
  uint32_t countAddress = address + le::Count * BytesPerWord;
  uint32_t indexAddress = address + le::Index * BytesPerWord;
  uint32_t count = 0;
  uint32_t index = 0;
  int result = readBytes(countAddress, BytesPerWord, &count);
  bool repeats = static_cast<int32_t>(count) > 1;
  if (!result && repeats) result = readBytes(indexAddress, BytesPerWord, &index);
  if (!result) result = storeBytes(countAddress, BytesPerWord, count - 1U);
  if (!result && repeats) {
    result = storeBytes(indexAddress, BytesPerWord, index + 1U);
    if (!result) cpuState.iptr -= offset;
  }
  if (result) {
    compat::println("ERROR: {}", memoryError(result));
    return -1;
  }
  return 0;
}

int ldl_(uint32_t index) {
  int result;
  uint32_t value;

  result = readWptrWord(index, &value);
  push(value);

  if (result) {
    compat::println("ERROR: {}", st20Error(result));
    return -1;
  }

  return 0;
}

int ldlp_(uint32_t index) {
  int result;
  uint32_t value;

  result = addrWptrWord(index, &value);
  push(value);

  if (result) {
    compat::println("ERROR: {}", st20Error(result));
    return -1;
  }

  return 0;
}

int ldmemstartval_(uint32_t /*unused*/) {
  push(memStartVal);
  return 0;
}

int ldnl_(uint32_t offset) {
  uint32_t oldAreg = pop();
  uint32_t cWord = 0;

  if (oldAreg & ByteSelectMask) {
    compat::println("WARNING: Attempt to access a word that is not on a word boundary");
  }

  uint32_t address = oldAreg + offset * BytesPerWord;

  int result = readBytes(address, BytesPerWord, &cWord);
  push(cWord);

  compat::note("NOTE: Read of memory address {:08x}, value=0x{:08x}", address, cWord);

  if (result) {
    compat::println("ERROR: {}", memoryError(result));
    compat::println("  Error occurred when executing ldnl {:08x}  iptr={:08x}", address,
                    get_iptr());
    return -1;
  }

  return 0;
}

int ldnlp_(uint32_t offset) {
  push(pop() + offset * BytesPerWord);
  return 0;
}

int ldpi_(uint32_t /*unused*/) {
  push(cpuState.iptr + pop());

  return 0;
}

int ldpri_(uint32_t /*unused*/) {
  push(cpuState.priority);

  return 0;
}

/*	Code: 68 FD
  Description: Perform a cold boot. Reset all machine states to the initial state and
  execute the boot microcode (boot from ROM: jump to the ROM entry point). */
int reboot_(uint32_t /*unused*/) {
  return rebootProcessor();
}

int ldprodid_(uint32_t /*unused*/) {
  push(st20ProductId);

  return 0;
}

int ldtraph_(uint32_t /*unused*/) {
  if (trapEnableMask(cpuState.priority) & LOADTRAP_TRAPENB)
    return requestTrap(LOADTRAP_TRAPENB, cpuState.priority);
  uint32_t group = pop();
  uint32_t buffer = pop();
  uint32_t priority = pop();
  if (priority > LOW_PRIORITY) return INVALID_PROCESS_LIST;
  return transferTrapRecord(group, static_cast<unsigned char>(priority), buffer, true, false);
}

int sttraph_(uint32_t /*unused*/) {
  if (trapEnableMask(cpuState.priority) & STORETRAP_TRAPENB)
    return requestTrap(STORETRAP_TRAPENB, cpuState.priority);
  uint32_t group = pop();
  uint32_t buffer = pop();
  uint32_t priority = pop();
  if (priority > LOW_PRIORITY) return INVALID_PROCESS_LIST;
  return transferTrapRecord(group, static_cast<unsigned char>(priority), buffer, false, false);
}

int ldtrapped_(uint32_t /*unused*/) {
  if (trapEnableMask(cpuState.priority) & LOADTRAP_TRAPENB)
    return requestTrap(LOADTRAP_TRAPENB, cpuState.priority);
  uint32_t group = pop();
  uint32_t buffer = pop();
  uint32_t priority = pop();
  if (priority > LOW_PRIORITY) return INVALID_PROCESS_LIST;
  return transferTrapRecord(group, static_cast<unsigned char>(priority), buffer, true, true);
}

int sttrapped_(uint32_t /*unused*/) {
  if (trapEnableMask(cpuState.priority) & STORETRAP_TRAPENB)
    return requestTrap(STORETRAP_TRAPENB, cpuState.priority);
  uint32_t group = pop();
  uint32_t buffer = pop();
  uint32_t priority = pop();
  if (priority > LOW_PRIORITY) return INVALID_PROCESS_LIST;
  return transferTrapRecord(group, static_cast<unsigned char>(priority), buffer, false, true);
}

int lmul_(uint32_t /*operand*/) {
  uint32_t oldAreg = pop();
  uint32_t oldBreg = pop();
  uint32_t oldCreg = pop();

  uint32_t al = oldAreg & 0xffff;
  uint32_t ah = (oldAreg >> 16) & 0xffff;

  uint32_t bl = oldBreg & 0xffff;
  uint32_t bh = (oldBreg >> 16) & 0xffff;

  uint32_t hval = ah * bh;
  uint32_t lval = al * bl;
  uint32_t temp1 = ah * bl;
  uint32_t temp2 = al * bh;

  uint32_t temp12;

  uint32_t carry = N_Add(lval, oldCreg, &bl);
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
uint32_t N_Add(uint32_t A, uint32_t B, uint32_t *C) {
  uint64_t sum = static_cast<uint64_t>(A) + B;
  *C = static_cast<uint32_t>(sum);
  return static_cast<uint32_t>(sum >> BitsPerWord);
}

int ls_(uint32_t /*unused*/) {
  int result;
  uint32_t oldAreg, newAreg;

  oldAreg = pop();
  result = readBytes(oldAreg, 2, &newAreg);
  push(newAreg);

  compat::note("NOTE: Read of memory address {:08x}, value=0x{:08x}", oldAreg, newAreg);

  if (result) {
    compat::println("ERROR: {}", memoryError(result));
    compat::println("  Error occurred when executing ls instruction");
    return -1;
  }

  return 0;
}

int lsx_(uint32_t /*unused*/) {
  uint32_t address = pop();
  uint32_t value = 0;
  int result = readBytes(address, 2, &value);
  push(static_cast<int16_t>(value));
  if (result) {
    compat::println("ERROR: {}", memoryError(result));
    return -1;
  }
  return 0;
}

int lshl_(uint32_t /*unused*/) {
  uint32_t count = pop();
  uint32_t low = pop();
  uint32_t high = pop();
  if (count >= BitsPerDoubleWord) {
    push(undefinedWord);
    push(undefinedWord);
    return 0;
  }
  uint64_t value = (static_cast<uint64_t>(high) << BitsPerWord) | low;
  uint64_t shifted = value << count;
  push(shifted >> BitsPerWord);
  push(shifted);
  return 0;
}

int lshr_(uint32_t /*unused*/) {
  uint32_t count = pop();
  uint32_t low = pop();
  uint32_t high = pop();
  if (count >= BitsPerDoubleWord) {
    push(undefinedWord);
    push(undefinedWord);
    return 0;
  }
  uint64_t value = (static_cast<uint64_t>(high) << BitsPerWord) | low;
  uint64_t shifted = value >> count;
  push(shifted >> BitsPerWord);
  push(shifted);
  return 0;
}

/* Code: 23 F8
  Description: Subtract with borrow in and check for overflow. The result of the
  operation, put into Areg, is Breg minus Areg, minus bit 0 of Creg.
*/
int lsub_(uint32_t /*unused*/) {
  int64_t Areg = static_cast<int32_t>(pop());
  int64_t Breg = static_cast<int32_t>(pop());
  int64_t Creg = static_cast<int32_t>(pop());

  int64_t diff = Breg - Areg - (Creg & 0x01);

  if (diff > 0x7FFFFFFFLL) {
    push(diff - 0x100000000LL);
    return signalArithmeticError(INTEGER_OVF_TRAPENB);
  } else if (diff < -2147483648LL) {
    push(diff + 0x100000000LL);
    return signalArithmeticError(INTEGER_OVF_TRAPENB);
  } else {
    push(diff);
  }

  return 0;
}

int lsum_(uint32_t /*unused*/) {
  uint32_t temp;

  uint32_t value1 = pop();
  uint32_t value2 = pop();
  uint32_t value3 = pop();

  uint32_t carry = N_Add(value1, (value3 & 0x01), &temp);
  carry = carry + N_Add(value2, temp, &value3);

  if (carry > 0) {
    push(1U);
    push(value3 - WordMask - 1);
  } else {
    push(0U);
    push(value3);
  }

  return 0;
}

int mint_(uint32_t /*unused*/) {
  push(MostNeg);

  return 0;
}

int devmove_(uint32_t /*unused*/) {
  uint32_t length = pop();
  uint32_t destination = pop();
  uint32_t source = pop();
  return copyDeviceBytes(source, destination, length);
}

int move_(uint32_t /*unused*/) {
  uint32_t oldAreg = pop();
  uint32_t oldBreg = pop();
  uint32_t oldCreg = pop();
  int result = storeByteRange(oldCreg, oldBreg, oldAreg);

  compat::note("NOTE: Copy of {:0x} bytes from address {:08x} to address {:08x}", oldAreg,
                  oldCreg, oldBreg);

  if (result) {
    compat::println("ERROR: {}", memoryError(result));
    compat::println("  Error occurred when executing move instruction");
    return -1;
  }

  return 0;
}

int move2dinit_(uint32_t /*unused*/) {
  cpuState.move2dBlockLength = pop();
  cpuState.move2dDestStride = pop();
  cpuState.move2dSourceStride = pop();
  return 0;
}

int move2dall_(uint32_t /*unused*/) {
  return copy2d(Move2dFilter::All);
}

int move2dnonzero_(uint32_t /*unused*/) {
  return copy2d(Move2dFilter::Nonzero);
}

int move2dzero_(uint32_t /*unused*/) {
  return copy2d(Move2dFilter::Zero);
}

int mul_(uint32_t /*unused*/) {
  int64_t oldAreg = static_cast<int32_t>(pop());
  int64_t oldBreg = static_cast<int32_t>(pop());
  int64_t product = oldAreg * oldBreg;
  push(product);
  return product > INT32_MAX || product < INT32_MIN ? signalArithmeticError(INTEGER_OVF_TRAPENB) : 0;
}

int norm_(uint32_t /*unused*/) {
  uint32_t low = pop();
  uint32_t high = pop();
  pop();
  uint64_t value = (static_cast<uint64_t>(high) << BitsPerWord) | low;
  int count = std::countl_zero(value);
  uint64_t normalized = value == 0 ? 0 : value << count;
  push(count);
  push(normalized >> BitsPerWord);
  push(normalized);
  return 0;
}

int nop_(uint32_t /*unused*/) {
  return 0;
}

int not_(uint32_t /*unused*/) {
  push(~pop());

  return 0;
}

int or_(uint32_t /*unused*/) {
  push(pop() | pop());

  return 0;
}

int out_(uint32_t /*unused*/) {
  uint32_t length = pop();
  uint32_t channel = pop();
  uint32_t buffer = pop();
  return transferChannel(channel, length, buffer, false);
}

int outbyte_(uint32_t /*unused*/) {
  uint32_t value = pop();
  uint32_t channel = pop();
  pop();
  return outputChannelValue(channel, value, 1);
}

int outword_(uint32_t /*unused*/) {
  uint32_t value = pop();
  uint32_t channel = pop();
  pop();
  return outputChannelValue(channel, value, BytesPerWord);
}

int pop_(uint32_t /*unused*/) {
  pop();

  return 0;
}

int prod_(uint32_t /*unused*/) {
  uint64_t oldAreg = pop();
  uint64_t oldBreg = pop();
  push(oldAreg * oldBreg);

  return 0;
}

int rem_(uint32_t /*unused*/) {
  int64_t divisor = static_cast<int32_t>(pop());
  int64_t dividend = static_cast<int32_t>(pop());
  if (divisor == 0) {
    push(undefinedWord);
    return signalArithmeticError(INTEGER_OVF_TRAPENB);
  }
  push(dividend % divisor);
  return 0;
}

int resetch_(uint32_t /*unused*/) {
  uint32_t channel = pop();
  uint32_t previous = undefinedWord;
  int result = resetChannel(channel, &previous);
  push(previous);
  return result;
}

int ret_(uint32_t /*unused*/) {
  int result;

  result = wptrPopState();
  if (result) {
    compat::println("ERROR: {}", st20Error(result));
    return -1;
  }

  return 0;
}

int rev_(uint32_t /*unused*/) {
  int result = 0;
  uint32_t oldAreg, oldBreg;

  oldAreg = pop();
  oldBreg = pop();

  push(oldAreg);
  push(oldBreg);

  return (result);
}

int runp_(uint32_t /*unused*/) {
  uint32_t descriptor = cpuState.areg;
  unsigned char priority = static_cast<unsigned char>(descriptor & 1U);
  if (trapEnableMask(priority) & RUN_TRAPENB)
    return requestTrap(RUN_TRAPENB, priority, false, descriptor);
  pop();
  pop();
  pop();
  int result = enqueueProcess(descriptor & WordSelectMask, static_cast<unsigned char>(descriptor & 1U));
  return result ? result : pollTimers();
}

int satadd_(uint32_t /*unused*/) {
  int64_t oldAreg = static_cast<int32_t>(pop());
  int64_t oldBreg = static_cast<int32_t>(pop());
  push(saturateWord(oldBreg + oldAreg));
  return 0;
}

int satsub_(uint32_t /*unused*/) {
  int64_t oldAreg = static_cast<int32_t>(pop());
  int64_t oldBreg = static_cast<int32_t>(pop());
  push(saturateWord(oldBreg - oldAreg));
  return 0;
}

int satmul_(uint32_t /*unused*/) {
  int64_t oldAreg = static_cast<int32_t>(pop());
  int64_t oldBreg = static_cast<int32_t>(pop());
  push(saturateWord(oldBreg * oldAreg));
  return 0;
}

int sb_(uint32_t /*value*/) {
  uint32_t value1 = pop();
  uint32_t value2 = pop();

  int result = storeBytes(value1, 1, static_cast<unsigned char>(value2 & 0xFF));

  compat::note("NOTE: Write to memory address {:08x}, value=0x{:08x}", value1, value2 & 0xFF);

  if (result) {
    compat::println("ERROR: {}", st20Error(result));
    return -1;
  }

  return 0;
}

int seterr_(uint32_t /*unused*/) {
  if (cpuState.priority == HIGH_PRIORITY) omrState.HP_ErrorFlag = 1;
  else omrState.LP_ErrorFlag = 1;
  if (omrState.HaltOnErrorFlag) {
    omrState.halted = true;
    return CPU_HALTED;
  }
  return 0;
}

int testerr_(uint32_t /*unused*/) {
  unsigned char &error = cpuState.priority == HIGH_PRIORITY ? omrState.HP_ErrorFlag : omrState.LP_ErrorFlag;
  push(error == 0 ? 1U : 0U);
  error = 0;
  return 0;
}

int stoperr_(uint32_t /*unused*/) {
  return (cpuState.priority == HIGH_PRIORITY ? omrState.HP_ErrorFlag : omrState.LP_ErrorFlag)
             ? suspendProcess() : 0;
}

int sethalterr_(uint32_t /*unused*/) {
  omrState.HaltOnErrorFlag = 1;
  if (cpuState.priority == HIGH_PRIORITY ? omrState.HP_ErrorFlag : omrState.LP_ErrorFlag) {
    omrState.halted = true;
    return CPU_HALTED;
  }
  return 0;
}

int testhalterr_(uint32_t /*unused*/) {
  push(omrState.HaltOnErrorFlag != 0 ? 1U : 0U);
  return 0;
}

/* Out-of-range shifts give zero, as the instruction set manual describes for shl and shr
 * (its formal definition says undefined); OS20's QueueActive |= 1 << -1 relies on it. */
int shl_(uint32_t /*unused*/) {
  uint32_t count = pop();
  uint32_t value = pop();
  uint32_t shifted = count < BitsPerWord ? value << count : 0;
  push(shifted);

  return 0;
}

int shr_(uint32_t /*unused*/) {
  uint32_t count = pop();
  uint32_t value = pop();
  uint32_t shifted = count < BitsPerWord ? value >> count : 0;
  push(shifted);

  return 0;
}

int signal_(uint32_t /*unused*/) {
  uint32_t semaphore = pop();
  pop();
  pop();
  return signalSemaphore(semaphore);
}

int slmul_(uint32_t /*unused*/) {
  int64_t oldAreg = static_cast<int32_t>(pop());
  int64_t oldBreg = static_cast<int32_t>(pop());
  uint32_t carry = pop();
  uint64_t product = static_cast<uint64_t>(oldAreg * oldBreg + carry);
  push(product >> BitsPerWord);
  push(product);
  return 0;
}

int sulmul_(uint32_t /*unused*/) {
  int64_t oldAreg = static_cast<int32_t>(pop());
  int64_t oldBreg = pop();
  uint32_t carry = pop();
  uint64_t product = static_cast<uint64_t>(oldAreg * oldBreg + carry);
  push(product >> BitsPerWord);
  push(product);
  return 0;
}

int ss_(uint32_t /*value*/) {
  uint32_t value1 = pop();
  uint32_t value2 = pop();

  int result = storeBytes(value1, 2, static_cast<unsigned int>(value2 & 0xFFFF));

  compat::note("NOTE: Write to memory address {:08x}, value=0x{:08x}", value1, value2 & 0xFFFF);

  if (result) {
    compat::println("ERROR: {}", st20Error(result));
    return -1;
  }

  return 0;
}

int ssub_(uint32_t /*unused*/) {
  uint32_t oldAreg;
  uint32_t oldBreg;

  oldAreg = pop();
  oldBreg = pop();

  push(oldAreg + oldBreg * 2);

  return 0;
}

int startp_(uint32_t /*unused*/) {
  uint32_t workspace = pop();
  uint32_t offset = pop();
  pop();
  int result = storeBytes(workspace + static_cast<uint32_t>(pw::Iptr) * BytesPerWord, BytesPerWord,
                          cpuState.iptr + offset);
  if (!result && (trapEnableMask(cpuState.priority) & RUN_TRAPENB))
    return requestTrap(RUN_TRAPENB, cpuState.priority, false, workspace | cpuState.priority);
  if (!result) result = enqueueProcess(workspace, cpuState.priority);
  return result ? result : pollTimers();
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

int stclock_(uint32_t /*unused*/) {
  uint32_t oldAreg;
  uint32_t oldBreg;

  oldAreg = pop();
  oldBreg = pop();

  compat::note("0x{:08x} stored in {} clock", oldBreg,
                  (oldAreg & 1) ? "low priority" : "high priority");

  if (oldAreg & 1)
    omrState.ClockRegLP = oldBreg;
  else
    omrState.ClockRegHP = oldBreg;

  return 0;
}

int stl_(uint32_t index) {
  int result;

  result = storeWptrWord(index, pop());

  if (result) {
    compat::println("ERROR: {}", st20Error(result));
    return -1;
  }

  return 0;
}

/*
Code: Function 0x0E
Description: Store the contents of Breg into the non-local variable at the specified
       word offset from Areg.
*/
int stnl_(uint32_t offset) {
  uint32_t oldAreg = pop();
  uint32_t oldBreg = pop();

  int result = storeBytes(oldAreg + offset * BytesPerWord, BytesPerWord, oldBreg);

  compat::note("NOTE: Write to memory address {:08x}, value=0x{:08x}", oldAreg + offset * BytesPerWord,
                  oldBreg);

  if (result) {
    compat::println("ERROR: {}", st20Error(result));
    return -1;
  }

  return 0;
}

int stopp_(uint32_t /*unused*/) {
  pop();
  pop();
  pop();

  return suspendProcess();
}

int sub_(uint32_t /*unused*/) {
  int64_t oldAreg = static_cast<int32_t>(pop());
  int64_t oldBreg = static_cast<int32_t>(pop());
  int64_t difference = oldBreg - oldAreg;
  push(difference);
  return difference > INT32_MAX || difference < INT32_MIN ? signalArithmeticError(INTEGER_OVF_TRAPENB) : 0;
}

int sum_(uint32_t /*unused*/) {
  push(pop() + pop());

  return 0;
}

int wait_(uint32_t /*unused*/) {
  uint32_t semaphore = pop();
  pop();
  pop();
  return waitSemaphore(semaphore);
}

int wcnt_(uint32_t /*unused*/) {
  uint32_t value1;

  value1 = pop();
  push(value1 & ByteSelectMask);
  push(static_cast<int32_t>(value1 & WordSelectMask) / BytesPerWord);

  return 0;
}

int wsub_(uint32_t /*unused*/) {
  uint32_t value1, value2;

  value1 = pop();
  value2 = pop();

  push(value1 + value2 * BytesPerWord);

  return 0;
}

int wsubdb_(uint32_t /*unused*/) {
  uint32_t value1, value2;

  value1 = pop();
  value2 = pop();

  push(value1 + value2 * BytesPerDoubleWord);

  return 0;
}

int xbword_(uint32_t /*unused*/) {
  int8_t value = static_cast<int8_t>(pop());
  push(value);
  return 0;
}

int xword_(uint32_t /*unused*/) {
  uint32_t signMask = pop();
  uint32_t value = pop();
  uint32_t extended = undefinedWord;
  if (signMask != 0 && (signMask & (signMask - 1)) == 0) {
    if (signMask == WordSignBit)
      extended = value;
    else if (value < (signMask << 1))
      extended = value < signMask ? value : value - (signMask << 1);
  }
  push(extended);
  return 0;
}

int xdble_(uint32_t /*unused*/) {
  uint32_t oldAreg = pop();
  push(static_cast<int32_t>(oldAreg) < 0 ? -1U : 0U);
  push(oldAreg);

  return 0;
}

int xor_(uint32_t /*unused*/) {
  push(pop() ^ pop());

  return 0;
}

int xsword_(uint32_t /*unused*/) {
  int16_t value = static_cast<int16_t>(pop());
  push(value);

  return 0;
}

/*
int _ (uint32_t value) {
  int result;

  if (result) {
   compat::println("ERROR: {}", st20Error(result));
   return -1;
  }

  return 0;
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

int ldtimer_(uint32_t /*unused*/) {
  if (cpuState.priority == HIGH_PRIORITY) {
    push(omrState.ClockRegHP);
    compat::note("Read of HIGH_PRIORITY timer. Value is 0x{:08x}", omrState.ClockRegHP);
  } else {
    push(omrState.ClockRegLP);
    compat::note("Read of LOW_PRIORITY timer. Value is 0x{:08x}", omrState.ClockRegLP);
  }
  return 0;
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

int sttimer_(uint32_t /*unused*/) {

  omrState.ClockRegHP = omrState.ClockRegLP = pop();
  compat::note("LOW and HIGH priority clock registers were set to 0x{:08x}",
                  omrState.ClockRegHP);

  omrState.ClockEnables |= HPTIMER_MASK; // set bit0
  omrState.ClockEnables |= LPTIMER_MASK; // set bit1

  return pollTimers();
}

int taltwt_(uint32_t /*unused*/) {
  pop();
  pop();
  pop();
  return waitTimedAlternative();
}

int tin_(uint32_t /*unused*/) {
  uint32_t time = pop();
  pop();
  pop();
  return waitTimer(time);
}

int swaptimer_(uint32_t /*unused*/) {
  uint32_t priority = pop();
  uint32_t front = pop();
  pop();
  if (priority > LOW_PRIORITY) return INVALID_PROCESS_LIST;
  uint32_t previous = NO_PROCESS;
  int result = swapTimerList(static_cast<unsigned char>(priority), front, &previous);
  push(previous);
  return result;
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

int clockdis_(uint32_t /*unused*/) {

  uint32_t oldAreg = pop();
  uint32_t oldClockEnables = omrState.ClockEnables;

  if (oldAreg & HPTIMER_MASK)
    omrState.ClockEnables &= ~HPTIMER_MASK; // clear bit0
  if (oldAreg & LPTIMER_MASK)
    omrState.ClockEnables &= ~LPTIMER_MASK; // clear bit1

  push(oldClockEnables &= ENABLE_CLOCKREG_MASK); // bit 0.1 leaved unchanged. others are zero'ed
                                 return 0;
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

int clockenb_(uint32_t /*unused*/) {

  uint32_t oldAreg = pop();
  uint32_t oldClockEnables = omrState.ClockEnables;

  if (oldAreg & HPTIMER_MASK)
    omrState.ClockEnables |= HPTIMER_MASK; // set bit0
  if (oldAreg & LPTIMER_MASK)
    omrState.ClockEnables |= LPTIMER_MASK; // set bit1

  push(oldClockEnables &= ENABLE_CLOCKREG_MASK); // bit 0.1 leaved unchanged. others are zero'ed
                                 return 0;
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
int ldclock_(uint32_t /*unused*/) {
  uint32_t oldAreg;

  oldAreg = pop();

  compat::note("Areg loaded with {} clock", (oldAreg & 1) ? "LOW_PRIORITY" : "HIGH_PRIORITY");

  if (oldAreg & 1)
    push(omrState.ClockRegLP);
  else
    push(omrState.ClockRegHP);


  return 0;
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
int tret_(uint32_t /*unused*/) {
  return returnFromTrap();
}

int trapdis_(uint32_t /*unused*/) {
  uint32_t mask = pop();
  uint32_t priority = pop();
  pop();
  if (priority > LOW_PRIORITY) return INVALID_PROCESS_LIST;
  uint32_t previous = 0;
  int result = changeTrapMask(static_cast<unsigned char>(priority), mask, false, &previous);
  push(previous);
  return result;
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
int trapenb_(uint32_t /*unused*/) {
  uint32_t mask = pop();
  uint32_t priority = pop();
  pop();
  if (priority > LOW_PRIORITY) return INVALID_PROCESS_LIST;
  uint32_t previous = 0;
  int result = changeTrapMask(static_cast<unsigned char>(priority), mask, true, &previous);
  push(previous);
  return result;
}

/*
GINTDIS (global interrupt disable)
Code: 2C FD
Description: Disable the global interrupt events specified in the bit mask in Areg.
This allows parts of the built-in scheduler, such as response to external events,
timeslicing etc., to be disabled by software. The original value of the global interrupt
enable register is returned in Areg.
Definition:
  GlobalInterruptEnables' <- GlobalInterruptEnables ^ ~Areg15..0
  Areg'15..0 <- GlobalInterruptEnables
  Areg'31..16 <- 0
Error signals: none
*/
int gintdis_(uint32_t /*unused*/) {
  uint32_t mask = (pop() & 0xffffU) << 16;
  uint32_t oldEnables = omrState.Enables >> 16;
  omrState.Enables &= ~mask;
  push(oldEnables);

  return 0;
}

/*
GINTENB (global interrupt enable)
Code: 2C FE
Description: Enable the global interrupt events specified in the bit mask in Areg.
Definition:
  GlobalInterruptEnables' <- GlobalInterruptEnables ! Areg15..0
  Areg'15..0 <- GlobalInterruptEnables
  Areg'16..31 <- 0
Error signals: none
*/
int gintenb_(uint32_t /*unused*/) {
  uint32_t mask = (pop() & 0xffffU) << 16;
  uint32_t oldEnables = omrState.Enables >> 16;
  omrState.Enables |= mask;
  push(oldEnables);

  return 0;
}
