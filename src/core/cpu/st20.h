#ifndef ST20_H
#define ST20_H

#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <span>
#include <type_traits>
#include <vector>

#include "../../common/defines.h"

/* this is the starting address for CPU execution */
#define START_ADDR 0x7FFFFFFE
#define START_ADDR_CH "START_ADDR"
extern uint32_t startAddr;

#define ST20_PRODUCT_ID 0x2D4C9041
#define ST20_PRODUCT_ID_CH "ST20_PRODUCT_ID"

#define MEM_START_VAL 0x80000140
#define MEM_START_VAL_CH "MEM_START_VAL"

#define MINIMUM_INTEGER 0x80000000
#define MAXIMUM_INTEGER 0x7FFFFFFF
inline constexpr int32_t MostPos = INT32_MAX;
inline constexpr int32_t MostNeg = INT32_MIN;

#define TIMER_GUESS 0x00000000
#define TIMER_GUESS_CH "TIMER_GUESS"

#define LOW_PRIORITY 1
#define HIGH_PRIORITY 0
#define NOT_PROCESS MINIMUM_INTEGER
inline constexpr int BitsPerByte = 8;
inline constexpr int BitsPerWord = 32;
inline constexpr int BytesPerWord = BitsPerWord / BitsPerByte;
inline constexpr int BitsPerDoubleWord = 2 * BitsPerWord;
inline constexpr int BytesPerDoubleWord = 2 * BytesPerWord;
inline constexpr uint32_t ByteSelectMask = 0x00000003U;
inline constexpr uint32_t WordSelectMask = 0xfffffffcU;
inline constexpr uint32_t WordMask = 0xffffffffU;
inline constexpr uint32_t WordSignBit = 1U << (BitsPerWord - 1);
inline constexpr uint32_t PeripheralStart = 0x20000000U;
inline constexpr uint32_t PeripheralEnd = 0x3fffffffU;

namespace pw {
inline constexpr int32_t Temp = 0;
inline constexpr int32_t Iptr = -1;
inline constexpr int32_t Link = -2;
inline constexpr int32_t Pointer = -3;
inline constexpr int32_t State = Pointer;
inline constexpr int32_t TLink = -4;
inline constexpr int32_t Time = -5;
}

namespace le {
inline constexpr int32_t Index = 0;
inline constexpr int32_t Count = 1;
}

namespace pp {
inline constexpr int32_t IptrSucc = 0;
inline constexpr int32_t Count = 1;
}

namespace s {
inline constexpr int32_t Count = 0;
inline constexpr int32_t Front = 1;
inline constexpr int32_t Back = 2;
}

inline constexpr uint32_t ALT_ENABLING = 0x80000001U;
inline constexpr uint32_t ALT_WAITING = 0x80000002U;
inline constexpr uint32_t ALT_READY = 0x80000003U;
inline constexpr uint32_t ALT_TIME_NOT_SET = 0x80000002U;
inline constexpr uint32_t ALT_TIME_SET = 0x80000001U;
inline constexpr uint32_t ALT_NONE_SELECTED = 0xffffffffU;

/* defines for 16 bit calculations */
#define NBITS 16
#define MAX_INT ((1 << NBITS) - 1)

#define WPTR_END_ADDR 0x1FFFFFFF
#define WPTR_END_ADDR_CH "WPTR_END_ADDR"
#define MAX_WPTR 0x2000
#define WPTR_PRINT_COLS 4

/* we map the lddevid opcode to a special code
 * to avoid conflicts with the ldprodid code
 */
#define LDDEVID 0x210

/* Error codes from st20.c */
#define ST20_ERROR_START -2000
#define ST20_ERROR_END -2999

#define BAD_WPTR -2000
#define WPTR_UNUSED -2001
#define WPTR_UNDERFLOW -2002
#define WPTR_OVERFLOW -2003
#define BAD_WATCH_CONDITION -2004
#define CPU_IDLE -2005
#define TIMER_TRAP_UNSUPPORTED -2006
#define INVALID_PROCESS_LIST -2007
#define RUN_TRAP_UNSUPPORTED -2008
#define SIGNAL_TRAP_UNSUPPORTED -2009
#define INVALID_CPU_FILENAME -2010
#define INVALID_CPU_FILE -2011
#define INVALID_CPU_WRITE -2012
#define INVALID_CPU_READ -2013
#define CHANNEL_HARDWARE_UNSUPPORTED -2014
#define CHANNEL_TRAP_UNSUPPORTED -2015
#define INVALID_CHANNEL -2016
#define INVALID_TRAP_HANDLER -2017
#define TRAP_REENTRY -2018
#define INVALID_TRAP_RETURN -2019
#define TRAP_HANDLER_BLOCKED -2020
#define CPU_HALTED -2021
#define INVALID_INTERRUPT_RETURN -2022
#define INTERRUPTED_OPERATION_UNSUPPORTED -2023
#define INVALID_SHADOW_ACCESS -2024
#define SHADOW_BLOCK_REGISTERS_UNSUPPORTED -2025
#define INVALID_PORTABLE_STATE -2026
#define PORTABLE_ABI_DISABLED -2027
#define INVALID_EXTERNAL_INTERRUPT -2028

void setPortableInstructionAbi(bool enabled);
bool portableInstructionAbiEnabled();

inline constexpr uint32_t STATUS_TIMESLICE_ENABLE = 1U << 20;
inline constexpr uint32_t STATUS_INTERRUPTED_OPERATION_MASK = 0x7c000000U;
inline constexpr uint32_t STATUS_VALID = 1U << 31;
inline constexpr uint32_t STATUS_PORTABLE_ABI = 1U << 25;
inline constexpr uint32_t STATUS_BUFFERED_WORD = 1U << 21;
inline constexpr uint32_t STATUS_BUFFER_WIDTH = 3U << 22;
inline constexpr uint32_t STATUS_PORTABLE_INTDIS = 1U << 24;

using InterruptReturnObserver = void (*)();
using PeripheralPoller = int (*)();
void setPeripheralPoller(PeripheralPoller poller);
void setInterruptReturnObserver(InterruptReturnObserver observer);
int queueExternalInterrupt(uint32_t level, uint32_t workspace, uint32_t iptr, uint32_t status = 0);
int serviceNativeInterrupt(uint32_t level, uint32_t handlerDescriptor, bool *accepted);
bool interruptHandlerActive();
int activeExternalInterruptLevel();
int registerExternalChannel(uint32_t address, uint32_t capacity = 4096);
int unregisterExternalChannel(uint32_t address);
int feedExternalChannel(uint32_t address, std::span<const uint8_t> bytes);
int drainExternalChannel(uint32_t address, std::span<uint8_t> bytes, std::size_t *count);

struct CPUSTATE {
  uint32_t areg{0};
  uint32_t breg{0};
  uint32_t creg{0};
  uint32_t iptr{0};
  int nWptr{0};
  unsigned char priority{LOW_PRIORITY};
  uint32_t wptr{0};
  bool running{true};
  uint32_t move2dBlockLength{0};
  uint32_t move2dDestStride{0};
  uint32_t move2dSourceStride{0};
  uint32_t status{STATUS_TIMESLICE_ENABLE};
  bool interruptsDisabled{false};
  uint32_t timeslicePeriods{0};
  uint32_t moveRowWidth{0};
  uint32_t moveBufferedWord{0};
};

struct CPUTrace {
  CPUSTATE state;
  uint32_t code{0};
  uint32_t operand{0};
  int length{0};
  std::array<unsigned char, 100> bytes{};
  std::vector<uint32_t> workspace;
};

int captureCPUTrace(CPUTrace &trace);
int printCPUState(const CPUTrace &trace);
int printNextInstr(const CPUTrace &trace);

uint32_t pop(void);
int push(uint32_t value);

template <typename T>
  requires std::is_integral_v<T>
int push(T value) {
  return push(static_cast<uint32_t>(value));
}

typedef struct watch_struct {
  bool watchAreg;
  uint32_t areg;
  bool watchBreg;
  uint32_t breg;
  bool watchCreg;
  uint32_t creg;
  bool watchIptr;
  uint32_t iptr;
  bool watchNWptr;
  int nWptr;
} WATCH;

int st20Init(PARMS *);
int initCPUState(void);
int initCPUState(bool analysed);
int loadCPUState(const char *);
int saveCPUState(const char *);
std::size_t cpuRuntimeSnapshotSize();
int printCPUState();
int printEnablesRegState();
int printOMRState();
int decodeNextInstr();
int printNextInstr();
int execInstr(int *);
int initTimer();
int advanceTimers(uint64_t cycles);
void resetScheduler();
int setReadyQueue(uint32_t priority, uint32_t front, uint32_t back);
int insertReadyQueue(uint32_t priority, uint32_t front, uint32_t back);
int enqueueProcess(uint32_t workspace, unsigned char priority);
int pollTimers();
int suspendProcess();
int endParallel(uint32_t workspace);
int timesliceProcess();
int checkPeriodicTimeslice();
int beginResumableMove(uint32_t operation, uint32_t length, uint32_t destination, uint32_t source);
int resumePendingOperation();
bool validPendingOperation(const CPUSTATE &state);
int restartProcess(uint32_t record);
int waitSemaphore(uint32_t semaphore);
int signalSemaphore(uint32_t semaphore);
int transferChannel(uint32_t channel, uint32_t length, uint32_t buffer, bool input);
int outputChannelValue(uint32_t channel, uint32_t value, uint32_t length);
int resetChannel(uint32_t channel, uint32_t *previous);
int beginAlternative(bool timed);
int waitAlternative();
int waitTimedAlternative();
int enableAltTimer(bool enabled, uint32_t time);
int disableAltTimer(uint32_t offset, bool enabled, uint32_t time, uint32_t *selected);
int finishAlternative();
int enableAltChannel(bool enabled, uint32_t channel);
int enableAltSkip(bool enabled);
int disableAltChannel(uint32_t offset, bool enabled, uint32_t channel, uint32_t *selected);
int disableAltSkip(uint32_t offset, bool enabled, uint32_t *selected);
uint32_t trapRecordAddress(unsigned char priority, uint32_t group, bool trapped);
uint32_t trapEnableMask(unsigned char priority);
int changeTrapMask(unsigned char priority, uint32_t mask, bool enable, uint32_t *previous);
int transferTrapRecord(uint32_t group, unsigned char priority, uint32_t buffer, bool load, bool trapped);
int transferShadowRegisters(uint32_t selection, uint32_t priority, uint32_t buffer, bool load);
int requestTrap(uint32_t cause, unsigned char priority, bool generated = false, uint32_t descriptor = 0x80000000U);
int returnFromTrap();
int returnFromInterrupt();
bool trapHandlerActive();
int signalArithmeticError(uint32_t cause);
int waitTimer(uint32_t time, bool alternative = false);
int swapTimerList(unsigned char priority, uint32_t front, uint32_t *previous);
int setAreg(uint32_t);
int setBreg(uint32_t);
int setCreg(uint32_t);
int setIptr(uint32_t);
int setWptr(uint32_t);
int storeWptrWord(uint32_t, uint32_t);
int readWptrWord(uint32_t, uint32_t *);
int initWatch();
int setWatch(const char *, const char *);
bool anyWatch();
bool checkWatch();
const char *st20Error(int);

int addrWptrWord(uint32_t, uint32_t *);
int wptrPopState(void);
int wptrPushState(void);
int allocWptr(uint32_t);
uint32_t N_Add(uint32_t A, uint32_t B, uint32_t *C);
uint32_t get_iptr(void);

#endif
