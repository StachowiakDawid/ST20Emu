#include "st20.h"
#include <cinttypes>
#include "instrentry.h"
#include "omr.h"

#include "../../common/compat.h"
#include "../memory/memory.h"

// std
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <algorithm>
#include <array>
#include <unordered_map>
#include <deque>
#include <bit>
#include <utility>
#include <vector>

// cannot use static when sharing with other C objects
OMRSTATE omrState;
CPUSTATE cpuState;
WATCH watch;
uint32_t instrCode = 0;
uint32_t operand = START_ADDR;
unsigned char instrBytes[100];
int instrLength = 0;
uint32_t st20ProductId = ST20_PRODUCT_ID;
uint32_t memStartVal = MEM_START_VAL;
uint32_t timerGuess = TIMER_GUESS;
uint32_t startAddr = START_ADDR;
uint32_t wptrEndAddr = WPTR_END_ADDR;

int initTimer();
void resetTimers();

extern INSTRENTRY instrEntry[];

namespace {
struct CPUSnapshotPrefix {
  uint32_t areg;
  uint32_t breg;
  uint32_t creg;
  uint32_t iptr;
  uint32_t reservedStatus;
  int nWptr;
  bool reservedWorkspace[MAX_WPTR];
};
CPUSTATE lowPriorityShadow;
bool lowPriorityShadowValid = false;
CPUSTATE highPriorityShadow;
bool highPriorityShadowValid = false;
InterruptReturnObserver interruptReturnObserver = nullptr;
PeripheralPoller peripheralPoller = nullptr;
RebootObserver rebootObserver = nullptr;
uint64_t cpuCycles = 0;

/* The clocks and the timeslice phase follow cpuCycles lazily inside runInstructions: they are
 * brought up to date (syncClocks) when a tick is due, before an instruction that may look at
 * them, and whenever runInstructions returns. Everywhere else clockCycles == cpuCycles. */
uint64_t clockCycles = 0;   // cycles already applied to them
uint64_t nextClockTick = 0; // cpuCycles at which the next clock or timeslice tick falls

/* True while the scheduler has nothing to do after an instruction that leaves it alone (see
 * schedulerQuiet): runInstructions then skips the per-instruction timer and scheduler checks
 * for such instructions until a tick or a peripheral poll is due. Anything that may wake or
 * switch processes clears it. */
bool quiescent = false;
bool fastPathEnabled = true;

/* peripherals are polled every peripheralPollCycles, and right after a device register access
 * or an interrupt return, rather than after every instruction */
constexpr uint64_t peripheralPollCycles = 64;
uint64_t lastPeripheralPoll = 0;
bool peripheralPollRequested = true;

/* set when execInstr has just polled the scheduler; the decode that follows skips its own poll
 * while the process keeps running, since nothing can have changed in between */
bool freshPoll = false;
struct ExternalChannel {
  uint32_t capacity;
  std::deque<uint8_t> input;
  std::deque<uint8_t> output;
  uint32_t waiter{NO_PROCESS};
  uint32_t active{NO_PROCESS};
  uint32_t length{0};
  bool inputWait{true};
  bool queued{false};
  uint32_t altOwner{NO_PROCESS};
  bool altWake{false};
  bool altNotified{false};
};
std::unordered_map<uint32_t, ExternalChannel> externalChannels;
struct ExternalInterruptFrame {
  uint32_t level;
  bool replacedShadow;
  bool previousShadowValid;
  uint32_t trapDepth;
  CPUSTATE caller;
  CPUSTATE previousShadow;
};
std::vector<ExternalInterruptFrame> externalInterruptFrames;
bool executingInstruction = false;
struct TrapFrame {
  uint32_t cause;
  unsigned char priority;
  bool generated;
  CPUSTATE caller;
};
std::vector<TrapFrame> trapFrames;
struct PendingTrap {
  uint32_t cause{0};
  unsigned char priority{0};
  uint32_t descriptor{NO_PROCESS};
};
PendingTrap pendingTimerTrap;
/* low priority scheduler traps raised while high priority ran (run, signal): taken in the low
 * priority context once high priority is idle */
std::deque<PendingTrap> pendingLowTraps;
bool queueEmptyReported[2]{};

/* instructions decoded from memory, keyed by address and revalidated with the block's write
 * version, so prefix chains aren't re-read byte by byte on every execution */
struct DecodedInstr {
  uint32_t iptr;
  uint32_t version;
  uint32_t code;
  uint32_t operand;
  int length; // 0: empty slot
  unsigned char bytes[12];
};
constexpr std::size_t decodeCacheSize = std::size_t{1} << 16;
std::array<DecodedInstr, decodeCacheSize> decodeCache{};

/* Straight runs of decoded instructions, up to a control transfer, an instruction that may
 * touch the scheduler, or the end of the memory block, so the free-running loop doesn't look
 * every instruction up on its own. Reused while the memory block's write version holds. */
struct BlockInstr {
  int (*function)(uint32_t);
  uint32_t operand;
  uint32_t code;
  uint32_t cycles;
  uint32_t length;
  bool pure; // leaves the scheduler and the timers alone (see quiescent)
};
struct CodeBlock {
  uint32_t iptr;
  uint32_t version;
  int count; // 0: empty slot
  BlockInstr instrs[16];
};
constexpr std::size_t codeBlockCacheSize = std::size_t{1} << 12;
std::array<CodeBlock, codeBlockCacheSize> codeBlocks{};

/* records a workspace met while walking a process list; false when it was already met (a
 * cyclic list). Callers keep their buffer static so the per-instruction walks don't allocate. */
bool visitOnce(std::vector<uint32_t> &visited, uint32_t workspace) {
  if (std::ranges::find(visited, workspace) != visited.end()) return false;
  visited.push_back(workspace);
  return true;
}

uint32_t trapGroup(uint32_t cause) {
  unsigned int bit = static_cast<unsigned int>(std::countr_zero(cause));
  return bit == 0 ? TRAP_GROUP_BREAKPOINT : bit <= 2 ? TRAP_GROUP_ERROR
      : bit <= 5 ? TRAP_GROUP_SYSTEM : TRAP_GROUP_SCHEDULER;
}

void selectPriority(unsigned char priority) {
  omrState.TrapEnables[cpuState.priority] = omrState.Enables & 0x3fffU;
  cpuState.priority = priority;
  omrState.Enables = (omrState.Enables & ~0x3fffU) | omrState.TrapEnables[priority];
}

enum RuntimeField {
  RuntimeMagic, RuntimeVersion, RuntimeWorkspace, RuntimePriority, RuntimeRunning,
  RuntimeClockHP, RuntimeClockLP, RuntimePhaseHPLow, RuntimePhaseHPHigh,
  RuntimePhaseLPLow, RuntimePhaseLPHigh, RuntimeEnables, RuntimeClockEnables,
  RuntimeHPError, RuntimeLPError, RuntimeHaltError, RuntimeReadyFrontHP, RuntimeReadyFrontLP,
  RuntimeReadyBackHP, RuntimeReadyBackLP, RuntimeTimerFrontHP, RuntimeTimerFrontLP,
  RuntimeAlarmHP, RuntimeAlarmLP, RuntimeShadowValid, RuntimeShadowA, RuntimeShadowB,
  RuntimeShadowC, RuntimeShadowIptr, RuntimeShadowWptr, RuntimeShadowWords,
  RuntimeShadowPriority, RuntimeShadowRunning, RuntimeMove2dRows, RuntimeMove2dDestStride,
  RuntimeMove2dSourceStride, RuntimeShadowMove2dRows, RuntimeShadowMove2dDestStride,
  RuntimeShadowMove2dSourceStride, RuntimeStatus, RuntimeShadowStatus,
  RuntimeTrapEnableHP, RuntimeTrapEnableLP, RuntimeHalted, RuntimeQueueEmptyReported,
  RuntimePendingCause, RuntimePendingPriority, RuntimePendingDescriptor, RuntimeTrapDepth,
  RuntimeTrapFrameData, RuntimeAnalysedOnReset = RuntimeTrapFrameData + 8 * 15,
  RuntimeInterruptsDisabled, RuntimeShadowInterruptsDisabled, RuntimeTrapInterruptsDisabled,
  RuntimeTimeslicePhaseLow = RuntimeTrapInterruptsDisabled + 8, RuntimeTimeslicePhaseHigh,
  RuntimeTimeslicePeriods, RuntimeShadowTimeslicePeriods, RuntimeTrapTimeslicePeriods,
  RuntimeHighShadowData = RuntimeTrapTimeslicePeriods + 8,
  RuntimeExternalInterruptData = RuntimeHighShadowData + 15,
  RuntimeFieldCount = RuntimeExternalInterruptData + 1 + 8 * 32
};
using RuntimeSnapshot = std::array<uint32_t, RuntimeFieldCount>;
constexpr uint32_t runtimeSnapshotMagic = 0x53543230U;
constexpr uint32_t runtimeSnapshotVersion = 12;

std::array<uint32_t, 14> encodeCpu(const CPUSTATE &state) {
  return {state.areg, state.breg,
      state.creg, state.iptr,
      static_cast<uint32_t>(state.nWptr), state.priority, state.wptr, state.running,
      state.move2dBlockLength, state.move2dDestStride, state.move2dSourceStride, state.status,
      state.interruptsDisabled, state.timeslicePeriods};
}

CPUSTATE decodeCpu(const uint32_t *words) {
  return {words[0], words[1], words[2],
      words[3], static_cast<int>(words[4]), static_cast<unsigned char>(words[5]),
      words[6], words[7] != 0, words[8], words[9], words[10], words[11], words[12] != 0,
      words[13]};
}

bool validCpuWords(const uint32_t *words) {
  return words[4] <= MAX_WPTR && words[5] <= LOW_PRIORITY && words[7] <= 1 && words[12] <= 1 &&
         words[13] <= 2 && (!words[7] || !(words[6] & 3U)) && !(words[11] & STATUS_INTERRUPTED_OPERATION_MASK);
}

RuntimeSnapshot captureRuntime() {
  RuntimeSnapshot snapshot{runtimeSnapshotMagic, runtimeSnapshotVersion, cpuState.wptr, cpuState.priority, cpuState.running,
          omrState.ClockRegHP, omrState.ClockRegLP,
          static_cast<uint32_t>(omrState.ClockPhaseHP), static_cast<uint32_t>(omrState.ClockPhaseHP >> 32),
          static_cast<uint32_t>(omrState.ClockPhaseLP), static_cast<uint32_t>(omrState.ClockPhaseLP >> 32),
          omrState.Enables, omrState.ClockEnables, omrState.HP_ErrorFlag,
          omrState.LP_ErrorFlag, omrState.HaltOnErrorFlag,
          omrState.ProcQueueFPtr[0], omrState.ProcQueueFPtr[1],
          omrState.ProcQueueBPtr[0], omrState.ProcQueueBPtr[1],
          omrState.TptrReg[0], omrState.TptrReg[1], omrState.TnextReg[0], omrState.TnextReg[1],
          lowPriorityShadowValid, lowPriorityShadow.areg,
          lowPriorityShadow.breg, lowPriorityShadow.creg,
          lowPriorityShadow.iptr, lowPriorityShadow.wptr,
          static_cast<uint32_t>(lowPriorityShadow.nWptr), lowPriorityShadow.priority,
          lowPriorityShadow.running, cpuState.move2dBlockLength, cpuState.move2dDestStride,
          cpuState.move2dSourceStride, lowPriorityShadow.move2dBlockLength,
          lowPriorityShadow.move2dDestStride, lowPriorityShadow.move2dSourceStride};
  omrState.TrapEnables[cpuState.priority] = trapEnableMask(cpuState.priority);
  snapshot[RuntimeStatus] = cpuState.status;
  snapshot[RuntimeShadowStatus] = lowPriorityShadow.status;
  snapshot[RuntimeTrapEnableHP] = omrState.TrapEnables[0];
  snapshot[RuntimeTrapEnableLP] = omrState.TrapEnables[1];
  snapshot[RuntimeHalted] = omrState.halted;
  snapshot[RuntimeAnalysedOnReset] = omrState.analysedOnReset;
  snapshot[RuntimeInterruptsDisabled] = cpuState.interruptsDisabled;
  snapshot[RuntimeShadowInterruptsDisabled] = lowPriorityShadow.interruptsDisabled;
  snapshot[RuntimeTimeslicePhaseLow] = static_cast<uint32_t>(omrState.TimeslicePhase);
  snapshot[RuntimeTimeslicePhaseHigh] = static_cast<uint32_t>(omrState.TimeslicePhase >> 32);
  snapshot[RuntimeTimeslicePeriods] = cpuState.timeslicePeriods;
  snapshot[RuntimeShadowTimeslicePeriods] = lowPriorityShadow.timeslicePeriods;
  snapshot[RuntimeHighShadowData] = highPriorityShadowValid;
  auto highWords = encodeCpu(highPriorityShadow);
  std::copy(highWords.begin(), highWords.end(), snapshot.begin() + RuntimeHighShadowData + 1);
  snapshot[RuntimeExternalInterruptData] = static_cast<uint32_t>(externalInterruptFrames.size());
  for (std::size_t index = 0; index < externalInterruptFrames.size(); ++index) {
    const auto &frame = externalInterruptFrames[index];
    std::size_t offset = RuntimeExternalInterruptData + 1 + index * 32;
    snapshot[offset] = frame.level;
    snapshot[offset + 1] = frame.replacedShadow;
    snapshot[offset + 2] = frame.previousShadowValid;
    snapshot[offset + 3] = frame.trapDepth;
    auto callerWords = encodeCpu(frame.caller);
    auto shadowWords = encodeCpu(frame.previousShadow);
    std::copy(callerWords.begin(), callerWords.end(), snapshot.begin() + static_cast<std::ptrdiff_t>(offset + 4));
    std::copy(shadowWords.begin(), shadowWords.end(), snapshot.begin() + static_cast<std::ptrdiff_t>(offset + 18));
  }
  snapshot[RuntimeQueueEmptyReported] = (queueEmptyReported[0] ? 1U : 0U) | (queueEmptyReported[1] ? 2U : 0U);
  snapshot[RuntimePendingCause] = pendingTimerTrap.cause;
  snapshot[RuntimePendingPriority] = pendingTimerTrap.priority;
  snapshot[RuntimePendingDescriptor] = pendingTimerTrap.descriptor;
  snapshot[RuntimeTrapDepth] = static_cast<uint32_t>(trapFrames.size());
  for (std::size_t index = 0; index < trapFrames.size(); ++index) {
    const TrapFrame &frame = trapFrames[index];
    snapshot[RuntimeTrapInterruptsDisabled + index] = frame.caller.interruptsDisabled;
    snapshot[RuntimeTrapTimeslicePeriods + index] = frame.caller.timeslicePeriods;
    std::array<uint32_t, 15> words{frame.cause, frame.priority, frame.generated,
      static_cast<uint32_t>(frame.caller.areg), static_cast<uint32_t>(frame.caller.breg),
      static_cast<uint32_t>(frame.caller.creg), static_cast<uint32_t>(frame.caller.iptr),
      static_cast<uint32_t>(frame.caller.nWptr), frame.caller.priority, frame.caller.wptr,
      frame.caller.running, frame.caller.move2dBlockLength, frame.caller.move2dDestStride,
      frame.caller.move2dSourceStride, frame.caller.status};
    std::copy(words.begin(), words.end(), snapshot.begin() +
              static_cast<std::ptrdiff_t>(RuntimeTrapFrameData + index * words.size()));
  }
  return snapshot;
}

uint64_t snapshotPhase(const RuntimeSnapshot &snapshot, RuntimeField low, RuntimeField high) {
  return snapshot[low] | (static_cast<uint64_t>(snapshot[high]) << 32);
}

bool validRuntime(const RuntimeSnapshot &snapshot) {
  if (snapshot[RuntimeMagic] != runtimeSnapshotMagic ||
      snapshot[RuntimeVersion] != runtimeSnapshotVersion ||
      snapshot[RuntimePriority] > LOW_PRIORITY || snapshot[RuntimeRunning] > 1 ||
      snapshot[RuntimeInterruptsDisabled] > 1 || snapshot[RuntimeShadowInterruptsDisabled] > 1 ||
      snapshot[RuntimeTimeslicePeriods] > 2 || snapshot[RuntimeShadowTimeslicePeriods] > 2 ||
      snapshot[RuntimeClockEnables] > 3 || snapshot[RuntimeHPError] > 1 ||
      snapshot[RuntimeLPError] > 1 || snapshot[RuntimeHaltError] > 1 ||
      snapshot[RuntimeShadowValid] > 1 || snapshot[RuntimeShadowWords] > MAX_WPTR ||
      snapshot[RuntimeShadowPriority] > LOW_PRIORITY || snapshot[RuntimeShadowRunning] > 1 ||
      (snapshot[RuntimeWorkspace] & 3U) ||
      snapshotPhase(snapshot, RuntimePhaseHPLow, RuntimePhaseHPHigh) >= CPU_CLOCK ||
      snapshotPhase(snapshot, RuntimePhaseLPLow, RuntimePhaseLPHigh) >= CPU_CLOCK ||
      snapshotPhase(snapshot, RuntimeTimeslicePhaseLow, RuntimeTimeslicePhaseHigh) >= CPU_CLOCK) return false;
  if (snapshot[RuntimeShadowValid] &&
      ((snapshot[RuntimePriority] != HIGH_PRIORITY && snapshot[RuntimeTrapDepth] == 0) || snapshot[RuntimeShadowPriority] != LOW_PRIORITY ||
       !snapshot[RuntimeShadowRunning] || (snapshot[RuntimeShadowWptr] & 3U))) return false;
  for (std::size_t priority = 0; priority < 2; ++priority) {
    uint32_t front = snapshot[RuntimeReadyFrontHP + priority];
    uint32_t back = snapshot[RuntimeReadyBackHP + priority];
    if ((front & 3U) || (back & 3U) || (snapshot[RuntimeTimerFrontHP + priority] & 3U) ||
        ((front == NO_PROCESS) != (back == NO_PROCESS))) return false;
  }
  {
    for (std::size_t index = 0; index < 8; ++index)
      if (snapshot[RuntimeTrapInterruptsDisabled + index] > 1 ||
          snapshot[RuntimeTrapTimeslicePeriods + index] > 2) return false;
    if ((snapshot[RuntimeTrapEnableHP] & ~0x3fffU) || (snapshot[RuntimeTrapEnableLP] & ~0x3fffU) ||
        snapshot[RuntimeHalted] > 1 || snapshot[RuntimeAnalysedOnReset] > 1 || snapshot[RuntimeQueueEmptyReported] > 3 ||
        snapshot[RuntimeTrapDepth] > 8 || snapshot[RuntimePendingPriority] > LOW_PRIORITY ||
        (snapshot[RuntimePendingCause] != 0 && snapshot[RuntimePendingCause] != TIMER_TRAPENB)) return false;
    uint32_t occupied = 0;
    for (std::size_t index = 0; index < snapshot[RuntimeTrapDepth]; ++index) {
      std::size_t offset = RuntimeTrapFrameData + index * 15;
      uint32_t cause = snapshot[offset];
      uint32_t priority = snapshot[offset + 1];
      if (!cause || (cause & (cause - 1)) || (cause & ~0x3fffU) || priority > LOW_PRIORITY ||
          snapshot[offset + 2] > 1 || snapshot[offset + 7] > MAX_WPTR ||
          snapshot[offset + 8] > LOW_PRIORITY || (snapshot[offset + 9] & 3U) ||
          snapshot[offset + 10] > 1) return false;
      uint32_t bit = 1U << (priority * 4 + trapGroup(cause));
      if (occupied & bit) return false;
      occupied |= bit;
      if (index + 1 == snapshot[RuntimeTrapDepth] && priority != snapshot[RuntimePriority]) return false;
    }
  }
  if (snapshot[RuntimeShadowStatus] & STATUS_INTERRUPTED_OPERATION_MASK) return false;
  for (std::size_t index = 0; index < snapshot[RuntimeTrapDepth]; ++index)
    if (snapshot[RuntimeTrapFrameData + index * 15 + 14] & STATUS_INTERRUPTED_OPERATION_MASK) return false;
  if (snapshot[RuntimeHighShadowData] > 1 ||
      !validCpuWords(snapshot.data() + RuntimeHighShadowData + 1)) return false;
  uint32_t depth = snapshot[RuntimeExternalInterruptData];
  if (depth > 8) return false;
  uint32_t lastLevel = 0;
  for (std::size_t index = 0; index < depth; ++index) {
    std::size_t offset = RuntimeExternalInterruptData + 1 + index * 32;
    if (snapshot[offset] >= 8 || (index != 0 && snapshot[offset] <= lastLevel) ||
        snapshot[offset + 1] > 1 || snapshot[offset + 2] > 1 || snapshot[offset + 3] > snapshot[RuntimeTrapDepth] ||
        !validCpuWords(snapshot.data() + offset + 4) || !validCpuWords(snapshot.data() + offset + 18)) return false;
    lastLevel = snapshot[offset];
  }
  return true;
}

void restoreRuntime(const RuntimeSnapshot &snapshot) {
  quiescent = false;
  cpuState.wptr = snapshot[RuntimeWorkspace];
  cpuState.priority = static_cast<unsigned char>(snapshot[RuntimePriority]);
  cpuState.running = snapshot[RuntimeRunning] != 0;
  cpuState.move2dBlockLength = snapshot[RuntimeMove2dRows];
  cpuState.move2dDestStride = snapshot[RuntimeMove2dDestStride];
  cpuState.move2dSourceStride = snapshot[RuntimeMove2dSourceStride];
  cpuState.status = snapshot[RuntimeStatus];
  omrState.ClockRegHP = snapshot[RuntimeClockHP];
  omrState.ClockRegLP = snapshot[RuntimeClockLP];
  omrState.ClockPhaseHP = snapshotPhase(snapshot, RuntimePhaseHPLow, RuntimePhaseHPHigh);
  omrState.ClockPhaseLP = snapshotPhase(snapshot, RuntimePhaseLPLow, RuntimePhaseLPHigh);
  omrState.Enables = snapshot[RuntimeEnables];
  omrState.ClockEnables = static_cast<unsigned char>(snapshot[RuntimeClockEnables]);
  omrState.HP_ErrorFlag = static_cast<unsigned char>(snapshot[RuntimeHPError]);
  omrState.LP_ErrorFlag = static_cast<unsigned char>(snapshot[RuntimeLPError]);
  omrState.HaltOnErrorFlag = static_cast<unsigned char>(snapshot[RuntimeHaltError]);
  omrState.TrapEnables[0] = snapshot[RuntimeTrapEnableHP];
  omrState.TrapEnables[1] = snapshot[RuntimeTrapEnableLP];
  omrState.TrapEnables[cpuState.priority] = omrState.Enables & 0x3fffU;
  omrState.halted = snapshot[RuntimeHalted] != 0;
  omrState.analysedOnReset = snapshot[RuntimeAnalysedOnReset] != 0;
  cpuState.interruptsDisabled = snapshot[RuntimeInterruptsDisabled] != 0;
  omrState.TimeslicePhase = snapshotPhase(snapshot, RuntimeTimeslicePhaseLow, RuntimeTimeslicePhaseHigh);
  cpuState.timeslicePeriods = snapshot[RuntimeTimeslicePeriods];
  highPriorityShadowValid = snapshot[RuntimeHighShadowData] != 0;
  highPriorityShadow = decodeCpu(snapshot.data() + RuntimeHighShadowData + 1);
  externalInterruptFrames.clear();
  for (std::size_t index = 0; index < snapshot[RuntimeExternalInterruptData]; ++index) {
    std::size_t offset = RuntimeExternalInterruptData + 1 + index * 32;
    externalInterruptFrames.push_back({snapshot[offset], snapshot[offset + 1] != 0, snapshot[offset + 2] != 0,
        snapshot[offset + 3], decodeCpu(snapshot.data() + offset + 4), decodeCpu(snapshot.data() + offset + 18)});
  }
  queueEmptyReported[0] = (snapshot[RuntimeQueueEmptyReported] & 1U) != 0;
  queueEmptyReported[1] = (snapshot[RuntimeQueueEmptyReported] & 2U) != 0;
  pendingTimerTrap = {snapshot[RuntimePendingCause], static_cast<unsigned char>(snapshot[RuntimePendingPriority]),
                       snapshot[RuntimePendingDescriptor]};
  for (std::size_t priority = 0; priority < 2; ++priority) {
    omrState.ProcQueueFPtr[priority] = snapshot[RuntimeReadyFrontHP + priority];
    omrState.ProcQueueBPtr[priority] = snapshot[RuntimeReadyBackHP + priority];
    omrState.TptrReg[priority] = snapshot[RuntimeTimerFrontHP + priority];
    omrState.TnextReg[priority] = snapshot[RuntimeAlarmHP + priority];
  }
  lowPriorityShadowValid = snapshot[RuntimeShadowValid] != 0;
  lowPriorityShadow = {snapshot[RuntimeShadowA],
                       snapshot[RuntimeShadowB],
                       snapshot[RuntimeShadowC],
                       snapshot[RuntimeShadowIptr],
                       static_cast<int>(snapshot[RuntimeShadowWords]),
                       static_cast<unsigned char>(snapshot[RuntimeShadowPriority]),
                       snapshot[RuntimeShadowWptr], snapshot[RuntimeShadowRunning] != 0,
                       snapshot[RuntimeShadowMove2dRows], snapshot[RuntimeShadowMove2dDestStride],
                       snapshot[RuntimeShadowMove2dSourceStride], snapshot[RuntimeShadowStatus]};
  lowPriorityShadow.interruptsDisabled = snapshot[RuntimeShadowInterruptsDisabled] != 0;
  lowPriorityShadow.timeslicePeriods = snapshot[RuntimeShadowTimeslicePeriods];
  trapFrames.clear();
  for (std::size_t index = 0; index < snapshot[RuntimeTrapDepth]; ++index) {
    std::size_t offset = RuntimeTrapFrameData + index * 15;
    CPUSTATE caller{snapshot[offset + 3], snapshot[offset + 4],
      snapshot[offset + 5], snapshot[offset + 6],
      static_cast<int>(snapshot[offset + 7]), static_cast<unsigned char>(snapshot[offset + 8]),
      snapshot[offset + 9], snapshot[offset + 10] != 0, snapshot[offset + 11],
      snapshot[offset + 12], snapshot[offset + 13], snapshot[offset + 14]};
    caller.interruptsDisabled = snapshot[RuntimeTrapInterruptsDisabled + index] != 0;
    caller.timeslicePeriods = snapshot[RuntimeTrapTimeslicePeriods + index];
    trapFrames.push_back({snapshot[offset], static_cast<unsigned char>(snapshot[offset + 1]),
                         snapshot[offset + 2] != 0, caller});
  }
}

bool timeReached(uint32_t clock, uint32_t time) {
  return static_cast<int32_t>(clock - time) >= 0;
}

int readProcessWord(uint32_t workspace, int32_t offset, uint32_t &value) {
  uint32_t word = 0;
  int result = readBytes(workspace + static_cast<uint32_t>(offset) * BytesPerWord, BytesPerWord, &word);
  value = word;
  return result;
}

int writeProcessWord(uint32_t workspace, int32_t offset, uint32_t value) {
  return storeBytes(workspace + static_cast<uint32_t>(offset) * BytesPerWord, BytesPerWord,
                    value);
}

int releaseExternalOwner(uint32_t descriptor) {
  unsigned char priority = static_cast<unsigned char>(descriptor & 1U);
  if (trapEnableMask(priority) & EXTERNALCH_TRAPENB) {
    if (trapHandlerActive() || interruptHandlerActive() ||
        (cpuState.running && cpuState.priority == HIGH_PRIORITY && priority == LOW_PRIORITY) ||
        (cpuState.running && cpuState.priority == LOW_PRIORITY && cpuState.interruptsDisabled && priority == HIGH_PRIORITY))
      return CPU_IDLE;
    return requestTrap(EXTERNALCH_TRAPENB, priority, false, descriptor);
  }
  return enqueueProcess(descriptor & WordSelectMask, priority);
}

int transferNativeExternalBytes(ExternalChannel &endpoint, uint32_t buffer, uint32_t length, bool input) {
  uint64_t end = static_cast<uint64_t>(buffer) + length;
  if (end > (uint64_t{1} << 32)) return INVALID_CHANNEL;
  if (buffer <= PeripheralEnd && end > PeripheralStart) return DEVICE_ACCESS_UNSUPPORTED;
  for (uint32_t offset = 0; offset < length; ++offset)
    if (isMemoryDeviceAddress(buffer + offset)) return DEVICE_ACCESS_UNSUPPORTED;
  std::vector<uint8_t> bytes(length);
  if (input) {
    std::copy_n(endpoint.input.begin(), length, bytes.begin());
    for (uint32_t offset = 0; offset < length; ++offset) {
      int result = storeBytes(buffer + offset, 1, bytes[offset]);
      if (result) return result;
    }
    for (uint32_t offset = 0; offset < length; ++offset) endpoint.input.pop_front();
  } else {
    for (uint32_t offset = 0; offset < length; ++offset) {
      uint32_t value = 0;
      int result = readBytes(buffer + offset, 1, &value);
      if (result) return result;
      bytes[offset] = static_cast<uint8_t>(value);
    }
    endpoint.output.insert(endpoint.output.end(), bytes.begin(), bytes.end());
  }
  return 0;
}

int pollExternalChannels() {
  for (auto &[address, endpoint] : externalChannels) {
    (void)address;
    if (endpoint.waiter != NO_PROCESS && !endpoint.queued) {
      bool ready = endpoint.inputWait ? endpoint.input.size() >= endpoint.length
                                    : endpoint.capacity - endpoint.output.size() >= endpoint.length;
      if (ready) {
        if (endpoint.length != 0) {
          unsigned char priority = static_cast<unsigned char>(endpoint.waiter & 1U);
          if (trapEnableMask(priority) & EXTERNALCH_TRAPENB) {
            uint32_t handler = 0;
            int result = readProcessWord(trapRecordAddress(priority, TRAP_GROUP_SCHEDULER, false), 2, handler);
            if (result || handler == 0 || (handler & 3U)) return INVALID_TRAP_HANDLER;
          }
          uint32_t buffer = 0;
          int result = readProcessWord(endpoint.waiter & WordSelectMask, pw::Pointer, buffer);
          if (!result) result = transferNativeExternalBytes(endpoint, buffer, endpoint.length, endpoint.inputWait);
          if (result) return result;
          endpoint.length = 0;
        }
        int result = releaseExternalOwner(endpoint.waiter);
        if (result == CPU_IDLE) continue;
        if (result) return result;
        endpoint.queued = true;
        endpoint.active = endpoint.waiter;
      }
    }
    if (endpoint.altOwner != NO_PROCESS && !endpoint.input.empty() && !endpoint.altNotified) {
      uint32_t workspace = endpoint.altOwner & WordSelectMask;
      uint32_t state = 0;
      int result = readProcessWord(workspace, pw::State, state);
      if (result) return result;
      if (state == ALT_WAITING) endpoint.altWake = true;
      if (state == ALT_ENABLING || state == ALT_WAITING || endpoint.altWake) {
        result = writeProcessWord(workspace, pw::State, ALT_READY);
        if (result) return result;
        if (endpoint.altWake) {
          result = releaseExternalOwner(endpoint.altOwner);
          if (result == CPU_IDLE) continue;
          if (result) return result;
        }
        endpoint.altNotified = true;
      }
    }
  }
  return 0;
}

/* a process released by an external channel resumes with its transfer complete */
void finishExternalWait(const CPUSTATE &state) {
  uint32_t descriptor = state.wptr | state.priority;
  for (auto &[address, endpoint] : externalChannels) {
    (void)address;
    if (endpoint.waiter != descriptor || !endpoint.queued) continue;
    endpoint.waiter = NO_PROCESS;
    endpoint.queued = false;
    endpoint.active = NO_PROCESS;
    return;
  }
}

int transferExternalChannel(uint32_t address, uint32_t length, uint32_t buffer, bool input) {
  if (interruptHandlerActive()) return TRAP_HANDLER_BLOCKED;
  auto &endpoint = externalChannels.at(address);
  uint32_t descriptor = cpuState.wptr | cpuState.priority;
  if ((endpoint.active != NO_PROCESS && endpoint.active != descriptor) || endpoint.waiter != NO_PROCESS ||
      length > endpoint.capacity) return INVALID_CHANNEL;
  if (length == 0) return 0;
  bool ready = input ? endpoint.input.size() >= length : endpoint.capacity - endpoint.output.size() >= length;
  if (!ready) {
    if (trapHandlerActive() || interruptHandlerActive()) return TRAP_HANDLER_BLOCKED;
    int result = writeProcessWord(cpuState.wptr, pw::Pointer, buffer);
    if (!result) result = writeProcessWord(cpuState.wptr, -5, length);
    if (result) return result;
    endpoint.waiter = descriptor;
    endpoint.length = length;
    endpoint.inputWait = input;
    return suspendProcess();
  }
  return transferNativeExternalBytes(endpoint, buffer, length, input);
}

bool softwareChannelAddress(uint32_t address) {
  bool peripheral = address >= PeripheralStart && address <= PeripheralEnd;
  bool coreReserved = address >= 0x80000000U && address < memStartVal;
  return !peripheral && !coreReserved;
}

int channelPeer(uint32_t channel, uint32_t &descriptor) {
  if (!softwareChannelAddress(channel)) return CHANNEL_HARDWARE_UNSUPPORTED;
  if (channel & 3U) return INVALID_CHANNEL;
  return readProcessWord(channel, 0, descriptor);
}

int dispatchProcess() {
  if (trapHandlerActive() || interruptHandlerActive()) return 0;
  if (!cpuState.running && omrState.ProcQueueFPtr[cpuState.priority] == NO_PROCESS &&
      (cpuState.priority == HIGH_PRIORITY || omrState.ProcQueueFPtr[HIGH_PRIORITY] == NO_PROCESS) &&
      (trapEnableMask(cpuState.priority) & QUEUE_EMPTY_TRAPENB) && !queueEmptyReported[cpuState.priority]) {
    unsigned char priority = cpuState.priority;
    int result = requestTrap(QUEUE_EMPTY_TRAPENB, priority);
    if (!result) queueEmptyReported[priority] = true;
    return result;
  }
  /* An idle high priority with nothing to run and no interrupted low priority process drops
   * to low priority, whose scheduler then finds its own queue empty (OS20 runs its kernel
   * from the low priority QueueEmpty trap). */
  if (!cpuState.running && cpuState.priority == HIGH_PRIORITY && !lowPriorityShadowValid &&
      omrState.ProcQueueFPtr[HIGH_PRIORITY] == NO_PROCESS && omrState.ProcQueueFPtr[LOW_PRIORITY] == NO_PROCESS &&
      (trapEnableMask(LOW_PRIORITY) & QUEUE_EMPTY_TRAPENB) && !queueEmptyReported[LOW_PRIORITY]) {
    selectPriority(LOW_PRIORITY);
    CPUSTATE idle = lowPriorityShadow;
    idle.priority = LOW_PRIORITY;
    idle.running = false;
    cpuState = idle;
    int result = requestTrap(QUEUE_EMPTY_TRAPENB, LOW_PRIORITY);
    if (!result) queueEmptyReported[LOW_PRIORITY] = true;
    return result;
  }
  if (cpuState.running) {
    if (cpuState.priority == HIGH_PRIORITY || omrState.ProcQueueFPtr[HIGH_PRIORITY] == NO_PROCESS ||
      cpuState.interruptsDisabled ||
        !(omrState.Enables & LP_PROCESS_INT_ENB)) return 0;
  }
  unsigned char priority = omrState.ProcQueueFPtr[HIGH_PRIORITY] != NO_PROCESS
                               ? HIGH_PRIORITY : LOW_PRIORITY;
  if (!cpuState.running && priority == LOW_PRIORITY && lowPriorityShadowValid) {
    selectPriority(LOW_PRIORITY);
    cpuState = lowPriorityShadow;
    lowPriorityShadowValid = false;
    queueEmptyReported[LOW_PRIORITY] = false;
    return 0;
  }
  uint32_t workspace = omrState.ProcQueueFPtr[priority];
  if (workspace == NO_PROCESS) return 0;
  if (workspace & 3U) return INVALID_PROCESS_LIST;
  uint32_t iptr = 0;
  uint32_t next = NO_PROCESS;
  int result = readProcessWord(workspace, pw::Iptr, iptr);
  if (!result) result = readProcessWord(workspace, pw::Link, next);
  if (result) return result;
  bool interrupted = cpuState.running && cpuState.priority == LOW_PRIORITY && priority == HIGH_PRIORITY;
  CPUSTATE nextState{undefinedWord, undefinedWord,
      undefinedWord, iptr, 0, priority, workspace, true};
  if (cpuState.running) {
    lowPriorityShadow = cpuState;
    lowPriorityShadowValid = true;
  } else if (cpuState.priority == LOW_PRIORITY) {
    /* an idle low priority keeps its last process's registers for its next scheduler trap */
    lowPriorityShadow = cpuState;
  }
  selectPriority(priority);
  omrState.ProcQueueFPtr[priority] = next;
  if (next == NO_PROCESS) omrState.ProcQueueBPtr[priority] = NO_PROCESS;
  cpuState = nextState;
  finishExternalWait(cpuState);
  queueEmptyReported[priority] = false;
  if (interrupted && (trapEnableMask(priority) & PROCESS_TRAPENB))
    return requestTrap(PROCESS_TRAPENB, priority, false, workspace | priority);
  return 0;
}

[[gnu::noinline]] int releaseDueTimers(uint32_t oldHP, uint32_t oldLP, uint64_t ticksHP, uint64_t ticksLP) {
  for (unsigned char priority : {static_cast<unsigned char>(HIGH_PRIORITY),
                                 static_cast<unsigned char>(LOW_PRIORITY)}) {
    uint32_t enable = priority == HIGH_PRIORITY ? HP_TIMER_ALRM_ENB : LP_TIMER_ALRM_ENB;
    if (!(omrState.Enables & enable)) continue;
    uint32_t oldClock = priority == HIGH_PRIORITY ? oldHP : oldLP;
    uint64_t ticks = priority == HIGH_PRIORITY ? ticksHP : ticksLP;
    /* the alarm register holds the front process's time (as on silicon), so there is nothing
     * to release while it hasn't been reached */
    uint32_t alarm = omrState.TnextReg[priority];
    if (omrState.TptrReg[priority] == NO_PROCESS || (!timeReached(oldClock, alarm) && ticks < alarm - oldClock))
      continue;
    static std::vector<uint32_t> visited;
    visited.clear();
    while (omrState.TptrReg[priority] != NO_PROCESS) {
      uint32_t workspace = omrState.TptrReg[priority];
      if ((workspace & 3U) || !visitOnce(visited, workspace)) return INVALID_PROCESS_LIST;
      uint32_t time = 0;
      int result = readProcessWord(workspace, pw::Time, time);
      if (result) return result;
      if (!timeReached(oldClock, time) && ticks < time - oldClock) break;
      uint32_t state = 0;
      int stateResult = readProcessWord(workspace, pw::State, state);
      bool alternative = stateResult == 0 && (state == ALT_WAITING || state == ALT_READY);
      bool needsWakeup = !alternative || state == ALT_WAITING;
      /* While this priority's scheduler trap handler runs, its group is masked, but a timer
       * trap condition stays set and is taken once the handler returns (ST20-TP2 datasheet
       * 3.6.3): decide with the enables the trap return will restore. */
      uint32_t enables = trapEnableMask(priority);
      for (const TrapFrame &frame : trapFrames) {
        if (frame.priority == priority && trapGroup(frame.cause) == TRAP_GROUP_SCHEDULER) {
          uint32_t restored = 0;
          if (readProcessWord(trapRecordAddress(priority, TRAP_GROUP_SCHEDULER, true), 0, restored) == 0)
            enables = restored & 0x3fffU;
        }
      }
      bool trapped = needsWakeup && (enables & TIMER_TRAPENB) != 0;
      if (trapped) {
        uint32_t handlerWptr = 0;
        int checked = readProcessWord(trapRecordAddress(priority, TRAP_GROUP_SCHEDULER, false), 2, handlerWptr);
        if (checked || handlerWptr == 0 || (handlerWptr & 3U)) return INVALID_TRAP_HANDLER;
      }
      uint32_t next = NO_PROCESS;
      result = readProcessWord(workspace, pw::TLink, next);
      if (result) return result;
      uint32_t nextTime = omrState.TnextReg[priority];
      if (next != NO_PROCESS) {
        result = readProcessWord(next, pw::Time, nextTime);
        if (result) return result;
      }
      if (alternative) {
        uint32_t observed = priority == HIGH_PRIORITY ? omrState.ClockRegHP : omrState.ClockRegLP;
        result = writeProcessWord(workspace, pw::State, ALT_READY);
        if (!result) result = writeProcessWord(workspace, pw::Time, observed);
        if (!result) result = writeProcessWord(workspace, pw::TLink, ALT_TIME_SET);
        if (result) return result;
      }
      result = trapped || !needsWakeup ? 0 : enqueueProcess(workspace, priority);
      if (result) return result;
      omrState.TptrReg[priority] = next;
      omrState.TnextReg[priority] = nextTime;
      if (trapped) {
        pendingTimerTrap = {TIMER_TRAPENB, priority, workspace | priority};
        return 0;
      }
    }
  }
  return 0;
}

/* whether a timer queue's alarm has been reached, so releaseTimers has work to do */
inline bool timersDue(uint32_t oldHP, uint32_t oldLP, uint64_t ticksHP, uint64_t ticksLP) {
  if (pendingTimerTrap.cause) return false;
  auto due = [](uint32_t enable, unsigned char priority, uint32_t oldClock, uint64_t ticks) {
    uint32_t alarm = omrState.TnextReg[priority];
    return (omrState.Enables & enable) && omrState.TptrReg[priority] != NO_PROCESS &&
           (timeReached(oldClock, alarm) || ticks >= alarm - oldClock);
  };
  return due(HP_TIMER_ALRM_ENB, HIGH_PRIORITY, oldHP, ticksHP) || due(LP_TIMER_ALRM_ENB, LOW_PRIORITY, oldLP, ticksLP);
}

/* runs twice per instruction: check the alarms inline, and only walk the timer queues (a
 * large function) once one has been reached */
inline int releaseTimers(uint32_t oldHP, uint32_t oldLP, uint64_t ticksHP, uint64_t ticksLP) {
  if (!timersDue(oldHP, oldLP, ticksHP, ticksLP)) return 0;
  quiescent = false;
  return releaseDueTimers(oldHP, oldLP, ticksHP, ticksLP);
}

int prepareProcess() {
  if (omrState.halted) return CPU_HALTED;
  bool polled = std::exchange(freshPoll, false);
  if (polled && cpuState.running) return 0;
  quiescent = false;
  int result = pollTimers();
  if (result || cpuState.running) return result;
  uint64_t soonest = UINT64_MAX;
  for (uint32_t priority = 0; priority < 2; ++priority) {
    uint32_t clockEnable = priority == HIGH_PRIORITY ? HPTIMER_MASK : LPTIMER_MASK;
    uint32_t alarmEnable = priority == HIGH_PRIORITY ? HP_TIMER_ALRM_ENB : LP_TIMER_ALRM_ENB;
    if (omrState.TptrReg[priority] == NO_PROCESS || !(omrState.ClockEnables & clockEnable) ||
        !(omrState.Enables & alarmEnable)) continue;
    uint32_t clock = priority == HIGH_PRIORITY ? omrState.ClockRegHP : omrState.ClockRegLP;
    uint64_t phase = priority == HIGH_PRIORITY ? omrState.ClockPhaseHP : omrState.ClockPhaseLP;
    uint32_t frequency = priority == HIGH_PRIORITY ? HP_TIMER_HZ : LP_TIMER_HZ;
    uint64_t ticks = static_cast<uint32_t>(omrState.TnextReg[priority] - clock);
    uint64_t needed = ticks * CPU_CLOCK - phase;
    uint64_t cycles = (needed + frequency - 1) / frequency;
    soonest = std::min(soonest, cycles);
  }
  if (!peripheralPoller) {
    if (soonest == UINT64_MAX) return CPU_IDLE;
    result = advanceTimers(soonest);
    return result ? result : cpuState.running ? 0 : CPU_IDLE;
  }
  /* With peripherals, idle time passes in small steps so that their events (interrupts,
   * edges on input pins) wake processes at the right time, as on silicon. */
  constexpr uint64_t idleStep = 1000;
  constexpr uint64_t idleLimit = 10ULL * CPU_CLOCK;
  for (uint64_t idle = 0; idle < idleLimit && !cpuState.running; idle += idleStep) {
    uint64_t cycles = std::min(soonest, idleStep);
    result = advanceTimers(cycles);
    if (result) return result;
    if (soonest != UINT64_MAX) {
      soonest -= cycles;
      if (soonest == 0) break;
    }
  }
  return cpuState.running ? 0 : CPU_IDLE;
}
}

std::size_t cpuRuntimeSnapshotSize() {
  return sizeof(RuntimeSnapshot);
}

void resetScheduler() {
  quiescent = false;
  for (int priority = 0; priority < 2; ++priority) {
    omrState.ProcQueueFPtr[priority] = NO_PROCESS;
    omrState.ProcQueueBPtr[priority] = NO_PROCESS;
    omrState.TptrReg[priority] = NO_PROCESS;
    omrState.TnextReg[priority] = 0;
    queueEmptyReported[priority] = false;
  }
  lowPriorityShadowValid = false;
  highPriorityShadowValid = false;
  highPriorityShadow = {};
  externalInterruptFrames.clear();
  executingInstruction = false;
  trapFrames.clear();
  pendingTimerTrap = {};
  pendingLowTraps.clear();
  omrState.halted = false;
  cpuState.running = true;
  cpuState.status = 0;
  cpuState.interruptsDisabled = false;
  cpuState.timeslicePeriods = 0;
  lowPriorityShadow.timeslicePeriods = 0;
  omrState.TimeslicePhase = 0;
}

int setReadyQueue(uint32_t priority, uint32_t front, uint32_t back) {
  if (priority > LOW_PRIORITY) return INVALID_PROCESS_LIST;
  omrState.ProcQueueFPtr[priority] = front;
  omrState.ProcQueueBPtr[priority] = back;
  if (front != NO_PROCESS) queueEmptyReported[priority] = false;
  return 0;
}

int insertReadyQueue(uint32_t priority, uint32_t front, uint32_t back) {
  if (priority > LOW_PRIORITY) return INVALID_PROCESS_LIST;
  if (front == NO_PROCESS) return 0;
  if ((front & 3U) || (back & 3U) || back == NO_PROCESS) return INVALID_PROCESS_LIST;
  uint32_t oldFront = omrState.ProcQueueFPtr[priority];
  if (oldFront != NO_PROCESS) {
    int result = writeProcessWord(back, pw::Link, oldFront);
    if (result) return result;
    back = omrState.ProcQueueBPtr[priority];
  }
  return setReadyQueue(priority, front, back);
}

int enqueueProcess(uint32_t workspace, unsigned char priority) {
  quiescent = false;
  if (priority > LOW_PRIORITY || workspace == NO_PROCESS || (workspace & 3U))
    return INVALID_PROCESS_LIST;
  int result = writeProcessWord(workspace, pw::Link, NO_PROCESS);
  if (result) return result;
  if (omrState.ProcQueueFPtr[priority] == NO_PROCESS)
    omrState.ProcQueueFPtr[priority] = workspace;
  else {
    result = writeProcessWord(omrState.ProcQueueBPtr[priority], pw::Link, workspace);
    if (result) return result;
  }
  omrState.ProcQueueBPtr[priority] = workspace;
  queueEmptyReported[priority] = false;
  return 0;
}

int registerExternalChannel(uint32_t address, uint32_t capacity) {
  quiescent = false;
  if ((address & 3U) || capacity == 0 || capacity > 1048576 || externalChannels.size() >= 1024 ||
      externalChannels.contains(address) || executingInstruction) return INVALID_CHANNEL;
  externalChannels.emplace(address, ExternalChannel{capacity, {}, {}});
  return 0;
}

int unregisterExternalChannel(uint32_t address) {
  auto found = externalChannels.find(address);
  if (found == externalChannels.end() || found->second.waiter != NO_PROCESS ||
      found->second.active != NO_PROCESS || found->second.altOwner != NO_PROCESS || executingInstruction) return INVALID_CHANNEL;
  externalChannels.erase(found);
  return 0;
}

int feedExternalChannel(uint32_t address, std::span<const uint8_t> bytes) {
  auto found = externalChannels.find(address);
  if (found == externalChannels.end() || bytes.size() > found->second.capacity - found->second.input.size()) return INVALID_CHANNEL;
  found->second.input.insert(found->second.input.end(), bytes.begin(), bytes.end());
  return executingInstruction ? 0 : pollTimers();
}

int drainExternalChannel(uint32_t address, std::span<uint8_t> bytes, std::size_t *count) {
  auto found = externalChannels.find(address);
  if (found == externalChannels.end() || count == nullptr) return INVALID_CHANNEL;
  auto &output = found->second.output;
  *count = std::min(bytes.size(), output.size());
  for (std::size_t index = 0; index < *count; ++index) {
    bytes[index] = output.front();
    output.pop_front();
  }
  return executingInstruction ? 0 : pollTimers();
}

bool interruptHandlerActive() {
  return !externalInterruptFrames.empty();
}

int activeExternalInterruptLevel() {
  return externalInterruptFrames.empty() ? -1 : static_cast<int>(externalInterruptFrames.back().level);
}

static int enterExternalInterrupt(uint32_t level, uint32_t workspace, uint32_t iptr, uint32_t status) {
  CPUSTATE caller = cpuState;
  bool valid = caller.running && caller.priority == HIGH_PRIORITY;
  std::array<uint32_t, 6> words{valid ? caller.status | STATUS_VALID : 0U, caller.wptr,
      caller.iptr, caller.areg, caller.breg, caller.creg};
  for (int index = 0; index < (valid ? 6 : 1); ++index) {
    int result = writeProcessWord(workspace, index, words[static_cast<std::size_t>(index)]);
    if (result) return result;
  }
  bool replace = caller.running && caller.priority == LOW_PRIORITY;
  externalInterruptFrames.push_back({level, replace, lowPriorityShadowValid,
      static_cast<uint32_t>(trapFrames.size()), caller, lowPriorityShadow});
  if (replace) {
    lowPriorityShadow = caller;
    lowPriorityShadowValid = true;
  }
  selectPriority(HIGH_PRIORITY);
  cpuState.wptr = workspace;
  cpuState.iptr = iptr;
  cpuState.status = status;
  cpuState.areg = cpuState.breg = cpuState.creg = undefinedWord;
  cpuState.running = true;
  cpuState.interruptsDisabled = false;
  cpuState.timeslicePeriods = 0;
  return 0;
}

int serviceNativeInterrupt(uint32_t level, uint32_t handlerDescriptor, bool *accepted) {
  if (!accepted) return INVALID_EXTERNAL_INTERRUPT;
  *accepted = false;
  if (omrState.halted) return CPU_HALTED;
  if (level >= 8 || (handlerDescriptor & 2U) || (handlerDescriptor & WordSelectMask) == 0)
    return INVALID_EXTERNAL_INTERRUPT;
  uint32_t mask = cpuState.priority == LOW_PRIORITY ? LP_EXTERNALEVENT_ENB : HP_EXTERNALEVENT_ENB;
  if (!(omrState.Enables & mask) || static_cast<int>(level) <= activeExternalInterruptLevel() ||
      (cpuState.priority == LOW_PRIORITY && cpuState.interruptsDisabled) ||
      (cpuState.running && cpuState.priority == HIGH_PRIORITY && !interruptHandlerActive() &&
       (handlerDescriptor & 1U))) return 0;
  if (cpuState.status & STATUS_INTERRUPTED_OPERATION_MASK) return INTERRUPTED_OPERATION_UNSUPPORTED;
  uint32_t top = handlerDescriptor & WordSelectMask;
  uint32_t status = 0;
  uint32_t iptr = 0;
  int result = readBytes(top - 8U, BytesPerWord, &status);
  if (!result) result = readBytes(top - 4U, BytesPerWord, &iptr);
  if (result) return result;
  if (status & STATUS_INTERRUPTED_OPERATION_MASK) return INTERRUPTED_OPERATION_UNSUPPORTED;
  uint32_t workspace = top - 32U;
  result = enterExternalInterrupt(level, workspace, iptr, status);
  if (!result) *accepted = true;
  return result;
}

int pollTimers() {
  if (omrState.halted) return CPU_HALTED;
  int result = releaseTimers(omrState.ClockRegHP, omrState.ClockRegLP, 0, 0);
  if (result || executingInstruction) return result;
  if (peripheralPoller && (peripheralPollRequested || cpuCycles - lastPeripheralPoll >= peripheralPollCycles)) {
    peripheralPollRequested = false;
    lastPeripheralPoll = cpuCycles;
    result = peripheralPoller();
    if (result) return result;
  }
  /* the common case, after nearly every instruction: the process keeps running and nothing below
   * has work (no interrupt request or timer trap pending, no external channel, no preemption) */
  if (cpuState.running && !pendingTimerTrap.cause && pendingLowTraps.empty() && externalChannels.empty() &&
      (trapHandlerActive() || interruptHandlerActive() || cpuState.priority == HIGH_PRIORITY ||
       omrState.ProcQueueFPtr[HIGH_PRIORITY] == NO_PROCESS || cpuState.interruptsDisabled ||
       !(omrState.Enables & LP_PROCESS_INT_ENB)))
    return 0;
  result = pollExternalChannels();
  if (result) return result;
  if (!executingInstruction && !trapHandlerActive() && !interruptHandlerActive() && !pendingLowTraps.empty() &&
      (cpuState.priority == LOW_PRIORITY ||
       (!cpuState.running && omrState.ProcQueueFPtr[HIGH_PRIORITY] == NO_PROCESS))) {
    if (cpuState.priority == HIGH_PRIORITY) {
      /* high priority is idle: drop to the preempted low priority process, or the idle bank */
      selectPriority(LOW_PRIORITY);
      bool preempted = lowPriorityShadowValid;
      cpuState = lowPriorityShadow;
      cpuState.priority = LOW_PRIORITY;
      cpuState.running = preempted;
      lowPriorityShadowValid = false;
    }
    /* bit 31 of the trapped status tells the handler a process was running (OS20's scheduler
     * only saves a preempted task when it is set) */
    if (cpuState.running) cpuState.status |= STATUS_VALID;
    PendingTrap pending = pendingLowTraps.front();
    pendingLowTraps.pop_front();
    if (trapEnableMask(LOW_PRIORITY) & pending.cause)
      return requestTrap(pending.cause, LOW_PRIORITY, false, pending.descriptor);
    result = enqueueProcess(pending.descriptor & WordSelectMask, LOW_PRIORITY);
    return result ? result : dispatchProcess();
  }
  if (!executingInstruction && !trapHandlerActive() && !interruptHandlerActive() && pendingTimerTrap.cause) {
    if (cpuState.running && cpuState.priority == LOW_PRIORITY && cpuState.interruptsDisabled &&
        pendingTimerTrap.priority == HIGH_PRIORITY) return dispatchProcess();
    /* A low-priority timer trap is taken at low priority, as its handler runs there: it waits
     * while a high-priority process runs, and when the high-priority side is idle the
     * processor first drops to low priority (OS20 starts the woken task through the
     * low-priority trapped-process record, which a high-priority return would discard). */
    if (pendingTimerTrap.priority == LOW_PRIORITY && cpuState.priority == HIGH_PRIORITY && cpuState.running)
      return dispatchProcess();
    if (pendingTimerTrap.priority == LOW_PRIORITY && cpuState.priority == HIGH_PRIORITY && !cpuState.running) {
      int dispatched = dispatchProcess();
      if (dispatched) return dispatched;
      if (cpuState.priority == HIGH_PRIORITY) {
        if (cpuState.running || omrState.ProcQueueFPtr[HIGH_PRIORITY] != NO_PROCESS) return 0;
        selectPriority(LOW_PRIORITY); // nothing to run at either priority: idle low priority
        cpuState = lowPriorityShadow;
        cpuState.priority = LOW_PRIORITY;
        cpuState.running = false;
      }
    }
    PendingTrap pending = pendingTimerTrap;
    bool trapped = (trapEnableMask(pending.priority) & pending.cause) != 0;
    result = trapped ? requestTrap(pending.cause, pending.priority, false, pending.descriptor)
                     : enqueueProcess(pending.descriptor & WordSelectMask, pending.priority);
    if (result) return result;
    pendingTimerTrap = {};
    return trapped ? 0 : dispatchProcess();
  }
  return dispatchProcess();
}

static int suspendSavedProcess() {
  cpuState.running = false;
  cpuState.interruptsDisabled = false;
  cpuState.status = (cpuState.status & ~STATUS_VALID) | STATUS_TIMESLICE_ENABLE;
  cpuState.timeslicePeriods = 0;
  return executingInstruction ? 0 : dispatchProcess();
}

int suspendProcess() {
  if (trapHandlerActive() || interruptHandlerActive()) return TRAP_HANDLER_BLOCKED;
  int result = writeProcessWord(cpuState.wptr, pw::Iptr, cpuState.iptr);
  return result ? result : suspendSavedProcess();
}

int endParallel(uint32_t workspace) {
  if (trapHandlerActive() || interruptHandlerActive()) return TRAP_HANDLER_BLOCKED;
  if (workspace & 3U) return BAD_WPTR;
  uint32_t count = 0;
  int result = readProcessWord(workspace, pp::Count, count);
  if (result) return result;
  if (count == 1) {
    uint32_t iptr = 0;
    result = readProcessWord(workspace, pp::IptrSucc, iptr);
    if (result) return result;
    cpuState.wptr = workspace;
    cpuState.iptr = iptr;
    cpuState.running = true;
  } else {
    result = writeProcessWord(workspace, pp::Count, count - 1U);
    if (result) return result;
    cpuState.running = false;
  }
  cpuState.areg = undefinedWord;
  cpuState.breg = undefinedWord;
  cpuState.creg = undefinedWord;
  cpuState.interruptsDisabled = false;
  cpuState.status |= STATUS_TIMESLICE_ENABLE;
  cpuState.timeslicePeriods = 0;
  return executingInstruction ? 0 : dispatchProcess();
}

int checkPeriodicTimeslice() {
  if (!cpuState.running || cpuState.priority != LOW_PRIORITY || trapHandlerActive() ||
      cpuState.timeslicePeriods < 2 || cpuState.interruptsDisabled ||
      !(cpuState.status & STATUS_TIMESLICE_ENABLE) || !(omrState.Enables & LP_TIMESLICE_ENB)) return 0;
  if ((omrState.ProcQueueFPtr[HIGH_PRIORITY] != NO_PROCESS &&
       (omrState.Enables & LP_PROCESS_INT_ENB)) ||
      (pendingTimerTrap.cause && pendingTimerTrap.priority == HIGH_PRIORITY)) return 0;
  return timesliceProcess();
}

int timesliceProcess() {
  quiescent = false;
  if (omrState.ProcQueueFPtr[cpuState.priority] == NO_PROCESS) {
    cpuState.areg = cpuState.breg = cpuState.creg = undefinedWord;
    return 0;
  }
  if (trapHandlerActive() || interruptHandlerActive()) return TRAP_HANDLER_BLOCKED;
  CPUSTATE caller = cpuState;
  int result = writeProcessWord(cpuState.wptr, pw::Iptr, cpuState.iptr);
  if (result) return result;
  bool trapped = (trapEnableMask(cpuState.priority) & TIMESLICE_TRAPENB) != 0;
  if (!trapped) {
    result = enqueueProcess(cpuState.wptr, cpuState.priority);
    if (result) return result;
  }
  cpuState.areg = cpuState.breg = cpuState.creg = undefinedWord;
  cpuState.interruptsDisabled = false;
  cpuState.status |= STATUS_TIMESLICE_ENABLE;
  cpuState.timeslicePeriods = 0;
  cpuState.running = false;
  if (trapped) {
    result = requestTrap(TIMESLICE_TRAPENB, cpuState.priority, false, cpuState.wptr | cpuState.priority);
    if (result) cpuState = caller;
    return result;
  }
  return executingInstruction ? 0 : dispatchProcess();
}

bool trapHandlerActive() {
  return !trapFrames.empty();
}

int requestTrap(uint32_t cause, unsigned char priority, bool generated, uint32_t descriptor) {
  quiescent = false;
  if (omrState.halted) return CPU_HALTED;
  if (priority > LOW_PRIORITY || cause == 0 || (cause & (cause - 1)) || (cause & ~0x3fffU))
    return INVALID_TRAP_HANDLER;
  if (!(trapEnableMask(priority) & cause)) return 0;
  uint32_t group = trapGroup(cause);
  for (const TrapFrame &frame : trapFrames)
    if (frame.priority == priority && trapGroup(frame.cause) == group) return TRAP_REENTRY;
  if (trapFrames.size() >= 8) return TRAP_REENTRY;
  uint32_t handler = trapRecordAddress(priority, group, false);
  std::array<uint32_t, 4> descriptorWords{};
  for (int index = 0; index < 4; ++index) {
    int result = readProcessWord(handler, index, descriptorWords[static_cast<std::size_t>(index)]);
    if (result) return INVALID_TRAP_HANDLER;
  }
  if (descriptorWords[2] == 0 || (descriptorWords[2] & 3U)) return INVALID_TRAP_HANDLER;
  /* A low priority run or signal trap raised while high priority runs (a high priority process
   * running or signalling a low priority one) is taken at low priority: it waits until high
   * priority is idle and then traps the low priority context, the process high priority
   * preempted or the idle bank with its last process's Wptr (OS20 starts tasks this way and
   * records the task in that workspace). */
  if ((cause == RUN_TRAPENB || cause == SIGNAL_TRAPENB) && priority == LOW_PRIORITY &&
      cpuState.priority == HIGH_PRIORITY) {
    pendingLowTraps.push_back({cause, priority, descriptor});
    return 0;
  }
  CPUSTATE trappedState = cpuState;
  TrapFrame frame{cause, priority, generated, cpuState};
  uint32_t enables = (omrState.Enables & ~0x3fffU) | trapEnableMask(priority);
  /* bit 31 of the trapped status tells the handler whether a process was running (OS20's
   * scheduler only saves a preempted task when it is set) */
  uint32_t status = trappedState.status | cause | (generated ? 0x8000U : 0U);
  if (!trappedState.running) status &= ~STATUS_VALID; // nothing to resume: an idle priority
  if (group == TRAP_GROUP_SCHEDULER) status = (status & ~0x30000U) | (static_cast<uint32_t>(trappedState.priority) << 16);
  std::array<uint32_t, 4> saved{enables, status, trappedState.wptr, trappedState.iptr};
  uint32_t trapped = trapRecordAddress(priority, group, true);
  for (int index = 0; index < 4; ++index) {
    int result = writeProcessWord(trapped, index, saved[static_cast<std::size_t>(index)]);
    if (result) return result;
  }
  if (group == TRAP_GROUP_SCHEDULER && descriptor != NO_PROCESS) {
    int result = writeProcessWord(descriptorWords[2], 0, descriptor);
    if (result) return result;
  }
  trapFrames.push_back(frame);
  selectPriority(priority);
  omrState.Enables = (descriptorWords[0] & 0xffff0000U) |
                     (omrState.Enables & descriptorWords[0] & 0xffffU);
  cpuState.status = descriptorWords[1];
  if (group == TRAP_GROUP_SCHEDULER)
    cpuState.status = (cpuState.status & ~0x30000U) | (static_cast<uint32_t>(trappedState.priority) << 16);
  cpuState.wptr = descriptorWords[2];
  cpuState.iptr = descriptorWords[3];
  cpuState.nWptr = 0;
  cpuState.running = true;
  /* Areg, Breg and Creg are not saved and keep the trapped process's values (ST20-TP2
   * datasheet 3.6.3); the descriptor is passed in the handler's entry workspace word 0. A low
   * priority context handler runs in the low priority register bank. */

  return 0;
}

void setInterruptReturnObserver(InterruptReturnObserver observer) {
  interruptReturnObserver = observer;
}

void setPeripheralPoller(PeripheralPoller poller) {
  peripheralPoller = poller;
  peripheralPollRequested = true;
}

void setRebootObserver(RebootObserver observer) {
  rebootObserver = observer;
}

/* Cold boot as by reboot: every processor state returns to its reset value and execution
 * restarts at START_ADDR (boot from ROM). Memory, including external RAM, is kept. */
int rebootProcessor() {
  omrState = {};
  cpuState = {};
  int result = initCPUState(false);
  resetTimers();
  if (!result && rebootObserver) result = rebootObserver();
  cpuState.iptr = startAddr;
  compat::println("NOTE: reboot, restarting at 0x{:08x}", startAddr);
  peripheralPollRequested = true;
  quiescent = false;
  return result;
}

int returnFromInterrupt() {
  bool managed = interruptHandlerActive();
  if (cpuState.priority != HIGH_PRIORITY || (trapHandlerActive() && !managed) ||
      (managed && trapFrames.size() != externalInterruptFrames.back().trapDepth) || (cpuState.wptr & 3U))
    return INVALID_INTERRUPT_RETURN;
  std::array<uint32_t, 6> saved{};
  int result = readProcessWord(cpuState.wptr, 0, saved[0]);
  if (result) return result;
  if (saved[0] & STATUS_VALID) {
    if (saved[0] & STATUS_INTERRUPTED_OPERATION_MASK)
      return INTERRUPTED_OPERATION_UNSUPPORTED;
    for (int index = 1; index < 6; ++index) {
      result = readProcessWord(cpuState.wptr, index, saved[static_cast<std::size_t>(index)]);
      if (result) return result;
    }
    if (saved[1] & 3U) return INVALID_INTERRUPT_RETURN;
    CPUSTATE restored = managed ? externalInterruptFrames.back().caller : cpuState;
    restored.status = saved[0];
    restored.wptr = saved[1];
    restored.iptr = saved[2];
    restored.areg = saved[3];
    restored.breg = saved[4];
    restored.creg = saved[5];
    restored.priority = HIGH_PRIORITY;
    restored.running = true;
    cpuState = restored;
    cpuState.status = saved[0];
    cpuState.wptr = saved[1];
    cpuState.iptr = saved[2];
    cpuState.areg = saved[3];
    cpuState.breg = saved[4];
    cpuState.creg = saved[5];
    cpuState.nWptr = 0;
    cpuState.running = true;
    cpuState.interruptsDisabled = false;
  } else if (lowPriorityShadowValid) {
    if (lowPriorityShadow.status & STATUS_INTERRUPTED_OPERATION_MASK) return INTERRUPTED_OPERATION_UNSUPPORTED;
    selectPriority(LOW_PRIORITY);
    cpuState = lowPriorityShadow;
    lowPriorityShadowValid = false;
  } else if (managed && externalInterruptFrames.back().caller.priority == LOW_PRIORITY &&
             !externalInterruptFrames.back().caller.running) {
    /* the interrupt came while low priority was idle: idle again with the registers kept, as
     * the Wptr of the last descheduled process is what a following QueueEmpty trap records */
    selectPriority(LOW_PRIORITY);
    cpuState = externalInterruptFrames.back().caller;
  } else {
    selectPriority(LOW_PRIORITY);
    cpuState = {undefinedWord, undefinedWord,
                undefinedWord, 0, 0, LOW_PRIORITY, NO_PROCESS, false,
                0, 0, 0, 0};
  }
  if (managed) {
    auto frame = externalInterruptFrames.back();
    externalInterruptFrames.pop_back();
    if (frame.replacedShadow) {
      lowPriorityShadow = frame.previousShadow;
      lowPriorityShadowValid = frame.previousShadowValid;
    }
  }
  if (interruptReturnObserver) interruptReturnObserver();
  peripheralPollRequested = true;
  quiescent = false;
  return 0;
}

int returnFromTrap() {
  if (trapFrames.empty()) return INVALID_TRAP_RETURN;
  if (interruptHandlerActive() && trapFrames.size() <= externalInterruptFrames.back().trapDepth)
    return INVALID_TRAP_RETURN;
  const TrapFrame frame = trapFrames.back();
  uint32_t group = trapGroup(frame.cause);
  if (cpuState.priority != frame.priority)
    return INVALID_TRAP_RETURN;
  std::array<uint32_t, 4> saved{};
  uint32_t trapped = trapRecordAddress(frame.priority, group, true);
  unsigned char priority = group == TRAP_GROUP_SCHEDULER ? static_cast<unsigned char>((cpuState.status >> 16) & 3U)
                                    : frame.caller.priority;
  if (priority > LOW_PRIORITY) return INVALID_TRAP_RETURN;
  if (priority != frame.caller.priority) return INVALID_TRAP_RETURN;
  bool crossPriority = priority != frame.priority;
  for (int index = 0; index < (crossPriority ? 2 : 4); ++index) {
    int result = readProcessWord(trapped, index, saved[static_cast<std::size_t>(index)]);
    if (result) return result;
  }
  if (!crossPriority && (saved[2] & 3U)) return INVALID_TRAP_RETURN;
  omrState.Enables = (omrState.Enables & ~0xffff3fffU) | (saved[0] & 0xffff3fffU);
  omrState.TrapEnables[frame.priority] = saved[0] & 0x3fffU;
  selectPriority(priority);
  if (priority != frame.priority) cpuState = frame.caller;
  cpuState.priority = priority;
  if (!crossPriority) cpuState.status = saved[1] & ~frame.cause;
  if (!crossPriority && group == TRAP_GROUP_SCHEDULER)
    cpuState.status = (cpuState.status & ~0x30000U) | (frame.caller.status & 0x30000U);
  cpuState.interruptsDisabled = frame.caller.interruptsDisabled;
  cpuState.timeslicePeriods = crossPriority || saved[2] == frame.caller.wptr ? frame.caller.timeslicePeriods : 0;
  cpuState.move2dBlockLength = frame.caller.move2dBlockLength;
  cpuState.move2dDestStride = frame.caller.move2dDestStride;
  cpuState.move2dSourceStride = frame.caller.move2dSourceStride;
  if (frame.generated)
    cpuState.status = (cpuState.status & ~0x8000U) | (frame.caller.status & 0x8000U);
  if (!crossPriority) {
    cpuState.wptr = saved[2];
    cpuState.iptr = saved[3];
  }
  cpuState.nWptr = frame.caller.nWptr;
  cpuState.running = crossPriority ? frame.caller.running
      : (saved[1] & STATUS_VALID) != 0 || frame.caller.running || saved[2] != frame.caller.wptr ||
        saved[3] != static_cast<uint32_t>(frame.caller.iptr);
  if (cpuState.running) queueEmptyReported[cpuState.priority] = false;
  /* another handler at this priority (run, timer, signal...) may have given its scheduler
   * work, so it reports an empty queue again the next time it becomes idle */
  if (frame.cause != QUEUE_EMPTY_TRAPENB) queueEmptyReported[frame.priority] = false;
  trapFrames.pop_back();
  return 0;
}

int signalArithmeticError(uint32_t cause) {
  if (trapEnableMask(cpuState.priority) & cause) return requestTrap(cause, cpuState.priority);
  if (cpuState.priority == HIGH_PRIORITY) omrState.HP_ErrorFlag = 1;
  else omrState.LP_ErrorFlag = 1;
  if (omrState.HaltOnErrorFlag) {
    omrState.halted = true;
    return CPU_HALTED;
  }
  return 0;
}

int waitSemaphore(uint32_t semaphore) {
  uint32_t count = 0;
  int result = readProcessWord(semaphore, s::Count, count);
  if (result) return result;
  if (count != 0) return writeProcessWord(semaphore, s::Count, count - 1U);
  if (trapHandlerActive() || interruptHandlerActive()) return TRAP_HANDLER_BLOCKED;
  uint32_t front = NO_PROCESS;
  result = readProcessWord(semaphore, s::Front, front);
  if (result) return result;
  uint32_t back = NO_PROCESS;
  if (front != NO_PROCESS) {
    result = readProcessWord(semaphore, s::Back, back);
    if (result) return result;
    if (back == NO_PROCESS || (front & 2U) || (back & 2U)) return INVALID_PROCESS_LIST;
  }
  uint32_t descriptor = cpuState.wptr | cpuState.priority;
  result = writeProcessWord(cpuState.wptr, pw::Link, NO_PROCESS);
  if (result) return result;
  if (front == NO_PROCESS) result = writeProcessWord(semaphore, s::Front, descriptor);
  else result = writeProcessWord(back & WordSelectMask, pw::Link, descriptor);
  if (!result) result = writeProcessWord(semaphore, s::Back, descriptor);
  return result ? result : suspendProcess();
}

int signalSemaphore(uint32_t semaphore) {
  uint32_t front = NO_PROCESS;
  int result = readProcessWord(semaphore, s::Front, front);
  if (result) return result;
  if (front == NO_PROCESS) {
    uint32_t count = 0;
    result = readProcessWord(semaphore, s::Count, count);
    return result ? result : writeProcessWord(semaphore, s::Count, count + 1U);
  }
  if (front & 2U) return INVALID_PROCESS_LIST;
  unsigned char priority = static_cast<unsigned char>(front & 1U);
  bool trapped = (trapEnableMask(priority) & SIGNAL_TRAPENB) != 0;
  if (trapped) {
    uint32_t handlerWptr = 0;
    result = readProcessWord(trapRecordAddress(priority, TRAP_GROUP_SCHEDULER, false), 2, handlerWptr);
    if (result || handlerWptr == 0 || (handlerWptr & 3U)) return INVALID_TRAP_HANDLER;
  }
  uint32_t workspace = front & WordSelectMask;
  uint32_t next = NO_PROCESS;
  result = readProcessWord(workspace, pw::Link, next);
  if (result) return result;
  if (next != NO_PROCESS && (next & 2U)) return INVALID_PROCESS_LIST;
  result = writeProcessWord(semaphore, s::Front, next);
  if (!result && next == NO_PROCESS) result = writeProcessWord(semaphore, s::Back, NO_PROCESS);
  if (!result) result = trapped ? requestTrap(SIGNAL_TRAPENB, priority, false, front)
                                : enqueueProcess(workspace, priority);
  return result ? result : pollTimers();
}

int beginAlternative(bool timed) {
  int result = writeProcessWord(cpuState.wptr, pw::State, ALT_ENABLING);
  if (!result && timed) result = writeProcessWord(cpuState.wptr, pw::TLink, ALT_TIME_NOT_SET);
  return result;
}

int waitAlternative() {
  uint32_t state = 0;
  int result = readProcessWord(cpuState.wptr, pw::State, state);
  if (result) return result;
  if (state != ALT_READY && (trapHandlerActive() || interruptHandlerActive())) return TRAP_HANDLER_BLOCKED;
  if (state != ALT_READY) {
    result = writeProcessWord(cpuState.wptr, pw::Iptr, cpuState.iptr);
    if (result) return result;
  }
  result = writeProcessWord(cpuState.wptr, pw::Temp, ALT_NONE_SELECTED);
  if (result || state == ALT_READY) return result;
  result = writeProcessWord(cpuState.wptr, pw::State, ALT_WAITING);
  return result ? result : suspendSavedProcess();
}

int enableAltTimer(bool enabled, uint32_t time) {
  if (!enabled) return 0;
  uint32_t link = ALT_TIME_NOT_SET;
  int result = readProcessWord(cpuState.wptr, pw::TLink, link);
  if (result) return result;
  if (link == ALT_TIME_NOT_SET) {
    result = writeProcessWord(cpuState.wptr, pw::TLink, ALT_TIME_SET);
    return result ? result : writeProcessWord(cpuState.wptr, pw::Time, time);
  }
  if (link == ALT_TIME_SET) {
    uint32_t current = 0;
    result = readProcessWord(cpuState.wptr, pw::Time, current);
    if (!result && static_cast<int32_t>(current - time) > 0)
      result = writeProcessWord(cpuState.wptr, pw::Time, time);
  }
  return result;
}

int waitTimedAlternative() {
  uint32_t state = 0;
  int result = readProcessWord(cpuState.wptr, pw::State, state);
  if (result) return result;
  uint32_t clock = cpuState.priority == HIGH_PRIORITY ? omrState.ClockRegHP : omrState.ClockRegLP;
  if (state == ALT_READY) {
    result = writeProcessWord(cpuState.wptr, pw::Time, clock);
    return result ? result : writeProcessWord(cpuState.wptr, pw::Temp, ALT_NONE_SELECTED);
  }
  uint32_t link = 0;
  result = readProcessWord(cpuState.wptr, pw::TLink, link);
  if (result) return result;
  if (link == ALT_TIME_NOT_SET) return waitAlternative();
  if (link != ALT_TIME_SET) return INVALID_PROCESS_LIST;
  uint32_t time = 0;
  result = readProcessWord(cpuState.wptr, pw::Time, time);
  if (result) return result;
  if (static_cast<int32_t>(clock - time) > 0) {
    result = writeProcessWord(cpuState.wptr, pw::State, ALT_READY);
    if (!result) result = writeProcessWord(cpuState.wptr, pw::Time, clock);
    return result ? result : writeProcessWord(cpuState.wptr, pw::Temp, ALT_NONE_SELECTED);
  }
  if (trapHandlerActive() || interruptHandlerActive()) return TRAP_HANDLER_BLOCKED;
  result = writeProcessWord(cpuState.wptr, pw::Temp, ALT_NONE_SELECTED);
  return result ? result : waitTimer(time, true);
}

int disableAltTimer(uint32_t offset, bool enabled, uint32_t time, uint32_t *selected) {
  *selected = 0;
  if (enabled) {
    uint32_t link = 0;
    int result = readProcessWord(cpuState.wptr, pw::TLink, link);
    if (result) return result;
    if (link == ALT_TIME_SET) {
      uint32_t observed = 0;
      result = readProcessWord(cpuState.wptr, pw::Time, observed);
      if (result) return result;
      if (static_cast<int32_t>(observed - time) > 0) {
        result = disableAltSkip(offset, true, selected);
        if (result) return result;
      }
    }
  }
  unsigned char priority = cpuState.priority;
  uint32_t previous = NO_PROCESS;
  uint32_t cursor = omrState.TptrReg[priority];
  static std::vector<uint32_t> visited;
  visited.clear();
  while (cursor != NO_PROCESS) {
    if ((cursor & 3U) || !visitOnce(visited, cursor)) return INVALID_PROCESS_LIST;
    uint32_t next = NO_PROCESS;
    int result = readProcessWord(cursor, pw::TLink, next);
    if (result) return result;
    if (cursor == cpuState.wptr) {
      if (previous == NO_PROCESS) {
        uint32_t nextTime = omrState.TnextReg[priority];
        if (next != NO_PROCESS) {
          result = readProcessWord(next, pw::Time, nextTime);
          if (result) return result;
        }
        omrState.TptrReg[priority] = next;
        omrState.TnextReg[priority] = nextTime;
      } else {
        result = writeProcessWord(previous, pw::TLink, next);
        if (result) return result;
      }
      return writeProcessWord(cpuState.wptr, pw::TLink, ALT_TIME_NOT_SET);
    }
    previous = cursor;
    cursor = next;
  }
  return 0;
}

int finishAlternative() {
  uint32_t offset = 0;
  int result = readProcessWord(cpuState.wptr, pw::Temp, offset);
  if (!result) cpuState.iptr = cpuState.iptr + offset;
  return result;
}

int enableAltChannel(bool enabled, uint32_t channel) {
  if (externalChannels.contains(channel)) {
    if (interruptHandlerActive()) return TRAP_HANDLER_BLOCKED;
    if (!enabled) return 0;
    auto &endpoint = externalChannels.at(channel);
    uint32_t owner = cpuState.wptr | cpuState.priority;
    if (endpoint.altOwner != NO_PROCESS && endpoint.altOwner != owner) return INVALID_CHANNEL;
    endpoint.altOwner = owner;
    endpoint.altWake = endpoint.altNotified = false;
    return pollExternalChannels();
  }
  if (!enabled) return 0;
  uint32_t descriptor = NO_PROCESS;
  int result = channelPeer(channel, descriptor);
  if (result) return result;
  uint32_t own = cpuState.wptr | cpuState.priority;
  if (descriptor == NO_PROCESS) return writeProcessWord(channel, 0, own);
  if (descriptor != own) return writeProcessWord(cpuState.wptr, pw::State, ALT_READY);
  return 0;
}

int enableAltSkip(bool enabled) {
  return enabled ? writeProcessWord(cpuState.wptr, pw::State, ALT_READY) : 0;
}

int disableAltSkip(uint32_t offset, bool enabled, uint32_t *selected) {
  *selected = 0;
  if (!enabled) return 0;
  uint32_t current = ALT_NONE_SELECTED;
  int result = readProcessWord(cpuState.wptr, pw::Temp, current);
  if (result || current != ALT_NONE_SELECTED) return result;
  result = writeProcessWord(cpuState.wptr, pw::Temp, offset);
  if (!result) *selected = 1;
  return result;
}

int disableAltChannel(uint32_t offset, bool enabled, uint32_t channel, uint32_t *selected) {
  if (externalChannels.contains(channel)) {
    auto &endpoint = externalChannels.at(channel);
    *selected = 0;
    if (!enabled) return 0;
    int result = disableAltSkip(offset, !endpoint.input.empty(), selected);
    if (!result && endpoint.altOwner == (cpuState.wptr | cpuState.priority)) {
      endpoint.altOwner = NO_PROCESS;
      endpoint.altWake = endpoint.altNotified = false;
    }
    return result;
  }
  *selected = 0;
  if (!enabled) return 0;
  uint32_t descriptor = NO_PROCESS;
  int result = channelPeer(channel, descriptor);
  if (result || descriptor == NO_PROCESS) return result;
  if (descriptor == (cpuState.wptr | cpuState.priority)) return writeProcessWord(channel, 0, NO_PROCESS);
  return disableAltSkip(offset, true, selected);
}

int transferChannel(uint32_t channel, uint32_t length, uint32_t buffer, bool input) {
  if (interruptHandlerActive()) return TRAP_HANDLER_BLOCKED;
  if (externalChannels.contains(channel)) return transferExternalChannel(channel, length, buffer, input);
  uint32_t descriptor = NO_PROCESS;
  int result = channelPeer(channel, descriptor);
  if (result) return result;
  if (descriptor == NO_PROCESS) {
    if (trapHandlerActive()) return TRAP_HANDLER_BLOCKED;
    result = writeProcessWord(cpuState.wptr, pw::Pointer, buffer);
    if (!result) result = writeProcessWord(channel, 0, cpuState.wptr | cpuState.priority);
    return result ? result : suspendProcess();
  }
  uint32_t workspace = descriptor & WordSelectMask;
  if ((descriptor & 2U) || workspace == NO_PROCESS || workspace == cpuState.wptr)
    return INVALID_CHANNEL;
  unsigned char priority = static_cast<unsigned char>(descriptor & 1U);
  bool trapped = (trapEnableMask(priority) & INTERNALCH_TRAPENB) != 0;
  uint32_t peerBuffer = 0;
  result = readProcessWord(workspace, pw::Pointer, peerBuffer);
  if (result) return result;
  bool alternative = peerBuffer == ALT_ENABLING || peerBuffer == ALT_WAITING || peerBuffer == ALT_READY;
  if (trapped && (!alternative || peerBuffer == ALT_WAITING)) {
    uint32_t handlerWptr = 0;
    result = readProcessWord(trapRecordAddress(priority, TRAP_GROUP_SCHEDULER, false), 2, handlerWptr);
    if (result || handlerWptr == 0 || (handlerWptr & 3U)) return INVALID_TRAP_HANDLER;
  }
  if (peerBuffer == ALT_ENABLING || peerBuffer == ALT_WAITING || peerBuffer == ALT_READY) {
    if (input) return INVALID_CHANNEL;
    if (trapHandlerActive()) return TRAP_HANDLER_BLOCKED;
    result = writeProcessWord(cpuState.wptr, pw::Pointer, buffer);
    if (!result) result = writeProcessWord(cpuState.wptr, pw::Iptr, cpuState.iptr);
    if (!result) result = writeProcessWord(channel, 0, cpuState.wptr | cpuState.priority);
    if (!result) result = writeProcessWord(workspace, pw::State, ALT_READY);
    if (result) return result;
    cpuState.running = false;
    if (peerBuffer == ALT_WAITING) {
      result = trapped ? requestTrap(INTERNALCH_TRAPENB, priority, false, descriptor)
                       : enqueueProcess(workspace, priority);
      if (result) return result;
    }
    return pollTimers();
  }
  uint32_t source = input ? peerBuffer : buffer;
  uint32_t destination = input ? buffer : peerBuffer;
  for (uint32_t offset = 0; offset < length; ++offset) {
    uint32_t value = 0;
    result = readBytes(source + offset, 1, &value);
    if (!result) result = storeBytes(destination + offset, 1, value);
    if (result) return result;
  }
  result = writeProcessWord(channel, 0, NO_PROCESS);
  if (!result) result = trapped ? requestTrap(INTERNALCH_TRAPENB, priority, false, descriptor)
                                : enqueueProcess(workspace, priority);
  return result ? result : pollTimers();
}

int outputChannelValue(uint32_t channel, uint32_t value, uint32_t length) {
  if (interruptHandlerActive()) return TRAP_HANDLER_BLOCKED;
  if (externalChannels.contains(channel)) {
    if (interruptHandlerActive()) return TRAP_HANDLER_BLOCKED;
    int result = writeProcessWord(cpuState.wptr, pw::Temp, value);
    return result ? result : transferExternalChannel(channel, length, cpuState.wptr, false);
  }
  uint32_t peer = NO_PROCESS;
  int result = channelPeer(channel, peer);
  if (result) return result;
  if (peer == NO_PROCESS && trapHandlerActive()) return TRAP_HANDLER_BLOCKED;
  if (peer != NO_PROCESS && (trapEnableMask(static_cast<unsigned char>(peer & 1U)) & INTERNALCH_TRAPENB)) {
    uint32_t state = 0;
    result = readProcessWord(peer & WordSelectMask, pw::State, state);
    if (result) return result;
    if (state != ALT_ENABLING && state != ALT_READY) {
      uint32_t handlerWptr = 0;
      result = readProcessWord(trapRecordAddress(static_cast<unsigned char>(peer & 1U), TRAP_GROUP_SCHEDULER, false), 2, handlerWptr);
      if (result || handlerWptr == 0 || (handlerWptr & 3U)) return INVALID_TRAP_HANDLER;
    }
  }
  uint32_t buffer = cpuState.wptr;
  result = writeProcessWord(buffer, 0, value);
  return result ? result : transferChannel(channel, length, buffer, false);
}

int resetChannel(uint32_t channel, uint32_t *previous) {
  if (externalChannels.contains(channel)) {
    auto &endpoint = externalChannels.at(channel);
    *previous = endpoint.waiter;
    uint32_t capacity = endpoint.capacity;
    endpoint = ExternalChannel{capacity, {}, {}};
    return 0;
  }
  /* link channel words (0x80000000-0x8000001f): no link hardware to reset, but the manual's
   * word update still applies; loaders reset all eight before jumping to an application */
  bool linkChannel = channel >= 0x80000000U && channel < 0x80000020U;
  if (!softwareChannelAddress(channel) && !linkChannel) return CHANNEL_HARDWARE_UNSUPPORTED;
  if (channel & 3U) return INVALID_CHANNEL;
  int result = readProcessWord(channel, 0, *previous);
  if (result && result != READ_UNUSED_MEM) return result;
  return writeProcessWord(channel, 0, NO_PROCESS);
}

uint32_t trapRecordAddress(unsigned char priority, uint32_t group, bool trapped) {
  return 0x80000040U + static_cast<uint32_t>(priority) * 0x80U + group * 0x20U +
         (trapped ? 0x10U : 0U);
}

uint32_t trapEnableMask(unsigned char priority) {
  return priority == cpuState.priority ? omrState.Enables & 0x3fffU
                                       : omrState.TrapEnables[priority];
}

int changeTrapMask(unsigned char priority, uint32_t mask, bool enable, uint32_t *previous) {
  if (priority > LOW_PRIORITY) return INVALID_PROCESS_LIST;
  *previous = trapEnableMask(priority);
  uint32_t updated = enable ? *previous | (mask & 0x3fffU) : *previous & ~(mask & 0x3fffU);
  omrState.TrapEnables[priority] = updated;
  if (priority == cpuState.priority)
    omrState.Enables = (omrState.Enables & ~0x3fffU) | updated;
  return 0;
}

int transferTrapRecord(uint32_t group, unsigned char priority, uint32_t buffer, bool load, bool trapped) {
  if (group > TRAP_GROUP_SCHEDULER || priority > LOW_PRIORITY) return INVALID_PROCESS_LIST;
  uint32_t record = trapRecordAddress(priority, group, trapped);
  uint32_t source = load ? buffer : record;
  uint32_t destination = load ? record : buffer;
  std::array<uint32_t, 4> words{};
  for (int index = 0; index < 4; ++index) {
    int result = readProcessWord(source, index, words[static_cast<std::size_t>(index)]);
    if (result) return result;
  }
  for (int index = 0; index < 4; ++index) {
    int result = writeProcessWord(destination, index, words[static_cast<std::size_t>(index)]);
    if (result) return result;
  }
  return 0;
}

int transferShadowRegisters(uint32_t selection, uint32_t priority, uint32_t buffer, bool load) {
  if (priority > LOW_PRIORITY) return INVALID_SHADOW_ACCESS;
  selection &= 0x0fU;
  if (selection == 0) return 0;
  if (load && priority == cpuState.priority && (selection & 7U)) return INVALID_SHADOW_ACCESS;
  if ((selection & 6U) && priority != cpuState.priority && priority != LOW_PRIORITY)
    return INVALID_SHADOW_ACCESS;
  CPUSTATE *bank = priority == cpuState.priority ? &cpuState : priority == LOW_PRIORITY ? &lowPriorityShadow : &highPriorityShadow;
  bool bankValid = priority == cpuState.priority ? cpuState.running : priority == LOW_PRIORITY ? lowPriorityShadowValid : highPriorityShadowValid;
  TrapFrame *savedFrame = nullptr;
  if (priority != cpuState.priority) {
    for (auto frame = trapFrames.rbegin(); frame != trapFrames.rend(); ++frame) {
      if (frame->caller.priority == priority) {
        bank = &frame->caller;
        bankValid = bank->running;
        savedFrame = &*frame;
        break;
      }
    }
  }
  if (selection & 8U) {
    if (priority != cpuState.priority && priority != LOW_PRIORITY) return INVALID_SHADOW_ACCESS;
    if (bank->status & STATUS_INTERRUPTED_OPERATION_MASK) return INTERRUPTED_OPERATION_UNSUPPORTED;
  }
  /* The C2 core transfers four block-move words (7-10), not the five (7-11) the instruction
   * set manual lists: OS20 (os20i.h NO_MOVE2D_WORDS, "Instruction set manual wrong") packs its
   * per-priority queues so that word 11 belongs to the next queue. */
  auto selectedWord = [selection](std::size_t index) {
    return (selection & (index == 0 ? 1U : index <= 3 ? 2U : index <= 6 ? 4U : 8U)) != 0;
  };
  const uint32_t enablesMask = 0xffff3fffU;
  const uint32_t globalMask = enablesMask & 0xffff0000U;
  std::array<uint32_t, 11> words{};
  if (load) {
    for (std::size_t index = 0; index < words.size(); ++index) {
      if (!selectedWord(index)) continue;
      int result = readProcessWord(buffer, static_cast<int32_t>(index), words[index]);
      if (result) return result;
    }
    if (selection & 2U) {
      if (words[1] & STATUS_INTERRUPTED_OPERATION_MASK) return INTERRUPTED_OPERATION_UNSUPPORTED;
      if ((words[1] & STATUS_VALID) && (words[2] & 3U)) return INVALID_SHADOW_ACCESS;
    }
    CPUSTATE updated = *bank;
    bool valid = bankValid;
    updated.priority = static_cast<unsigned char>(priority);
    updated.running = valid;
    if (!valid) {
      updated.status &= ~STATUS_VALID;
      updated.interruptsDisabled = false;
    }
    if (selection & 2U) {
      updated.status = words[1];
      updated.wptr = words[2];
      updated.iptr = words[3];
      updated.nWptr = 0;
      updated.timeslicePeriods = 0;
      valid = (words[1] & STATUS_VALID) != 0;
      updated.running = valid;
      if (!valid) updated.interruptsDisabled = false;
    }
    if (selection & 4U) {
      updated.areg = words[4];
      updated.breg = words[5];
      updated.creg = words[6];
    }
    if (selection & 1U) {
      omrState.Enables = (omrState.Enables & ~globalMask) | (words[0] & globalMask);
      omrState.TrapEnables[priority] = words[0] & 0x3fffU;
    }
    if (selection & 14U) {
      *bank = updated;
      if (!savedFrame && priority != cpuState.priority) {
        if (priority == LOW_PRIORITY) lowPriorityShadowValid = valid;
        else highPriorityShadowValid = valid;
      }
    }
    return 0;
  }
  CPUSTATE source = *bank;
  bool valid = bankValid;
  if (!valid && !savedFrame) {
    source = {undefinedWord, undefinedWord,
              undefinedWord, undefinedWord, 0,
              static_cast<unsigned char>(priority), NO_PROCESS, false, 0, 0, 0,
              source.status & ~STATUS_VALID};
  }
  if (selection & 1U)
    words[0] = (omrState.Enables & globalMask) | trapEnableMask(static_cast<unsigned char>(priority));
  words[1] = valid ? source.status | STATUS_VALID : source.status & ~STATUS_VALID;
  words[2] = source.wptr;
  words[3] = source.iptr;
  words[4] = source.areg;
  words[5] = source.breg;
  words[6] = source.creg;
  /* the block-move words 7-10 are not modelled */
  for (std::size_t index = 7; index < words.size(); ++index) words[index] = undefinedWord;
  for (std::size_t index = 0; index < words.size(); ++index) {
    if (!selectedWord(index)) continue;
    int result = writeProcessWord(buffer, static_cast<int32_t>(index), words[index]);
    if (result) return result;
  }
  return 0;
}

int restartProcess(uint32_t record) {
  if (interruptHandlerActive()) return TRAP_HANDLER_BLOCKED;
  std::array<uint32_t, 7> words{};
  /* restart uses only the enables, control and evaluation-stack words */
  for (std::size_t index = 0; index < words.size(); ++index) {
    int result = readProcessWord(record, static_cast<int32_t>(index), words[index]);
    if (result) return result;
  }
  if (words[1] & STATUS_INTERRUPTED_OPERATION_MASK) return INTERRUPTED_OPERATION_UNSUPPORTED;
  if ((words[1] & STATUS_VALID) && (words[2] & 3U)) return INVALID_PROCESS_LIST;
  CPUSTATE updated{words[4], words[5], words[6],
      words[3], 0, cpuState.priority, words[2], (words[1] & STATUS_VALID) != 0,
      0, 0, 0, words[1]};
  uint32_t enablesMask = 0xffff3fffU;
  omrState.Enables = (omrState.Enables & ~enablesMask) | (words[0] & enablesMask);
  omrState.TrapEnables[cpuState.priority] = words[0] & 0x3fffU;
  trapFrames.clear();
  cpuState = updated;
  if (cpuState.running) queueEmptyReported[cpuState.priority] = false;
  return 0;
}

int waitTimer(uint32_t time, bool alternative) {
  unsigned char priority = cpuState.priority;
  uint32_t clock = priority == HIGH_PRIORITY ? omrState.ClockRegHP : omrState.ClockRegLP;
  if (static_cast<int32_t>(clock - time) > 0) return 0;
  if (trapHandlerActive() || interruptHandlerActive()) return TRAP_HANDLER_BLOCKED;
  uint32_t deadline = time + 1U;
  uint32_t previous = NO_PROCESS;
  uint32_t next = omrState.TptrReg[priority];
  static std::vector<uint32_t> visited;
  visited.clear();
  while (next != NO_PROCESS) {
    if ((next & 3U) || next == cpuState.wptr || !visitOnce(visited, next))
      return INVALID_PROCESS_LIST;
    uint32_t nextTime = 0;
    int result = readProcessWord(next, pw::Time, nextTime);
    if (result) return result;
    if (static_cast<int32_t>(nextTime - deadline) > 0) break;
    previous = next;
    result = readProcessWord(next, pw::TLink, next);
    if (result) return result;
  }
  int result = writeProcessWord(cpuState.wptr, pw::Iptr, cpuState.iptr);
  if (!result) result = writeProcessWord(cpuState.wptr, pw::State, alternative ? ALT_WAITING : ALT_ENABLING);
  if (!result) result = writeProcessWord(cpuState.wptr, pw::Time, deadline);
  if (!result) result = writeProcessWord(cpuState.wptr, pw::TLink, next);
  if (result) return result;
  if (previous == NO_PROCESS) {
    omrState.TptrReg[priority] = cpuState.wptr;
    omrState.TnextReg[priority] = deadline;
  } else {
    result = writeProcessWord(previous, pw::TLink, cpuState.wptr);
    if (result) return result;
  }
  return suspendSavedProcess();
}

int swapTimerList(unsigned char priority, uint32_t front, uint32_t *previous) {
  if (priority > LOW_PRIORITY || (front & 3U)) return INVALID_PROCESS_LIST;
  uint32_t time = omrState.TnextReg[priority];
  if (front != NO_PROCESS) {
    int result = readProcessWord(front, pw::Time, time);
    if (result) return result;
  }
  *previous = omrState.TptrReg[priority];
  omrState.TptrReg[priority] = front;
  omrState.TnextReg[priority] = time;
  return 0;
}

int st20Init() {
  initCPUState();
  initWatch();
  initTimer();

  return (0);
}

/**************************
 * Initialize the processor
 */
int initCPUState(void) {
  return initCPUState(false);
}

int initCPUState(bool analysed) {
  int result = 0;
  uint32_t address;
  resetScheduler();
  omrState.analysedOnReset = analysed;

  /* prepare to start the processing at 0x7FFFFFFE */
  instrCode = 0;
  operand = startAddr;
  instrLength = 0;

  /* initialize the state of the processor */
  cpuState.nWptr = 7;
  cpuState.priority = LOW_PRIORITY;
  cpuState.running = true;
  cpuState.status = STATUS_TIMESLICE_ENABLE;
  cpuState.wptr = wptrEndAddr + 1U - 28U;
  addrWptrWord(0, &address);
  result = allocBytes(address, 4 * 7);
  cpuState.areg = cpuState.breg = cpuState.creg = undefinedWord;
  cpuState.iptr = 0;

  return (result);
}

int initWatch() {
  watch.watchAreg = false;
  watch.watchBreg = false;
  watch.watchCreg = false;
  watch.watchIptr = false;
  watch.watchNWptr = false;

  return 0;
}

int saveCPUState(const char *dirName) {
  if (dirName == nullptr || *dirName == '\0') {
    return INVALID_CPU_FILENAME;
  }

  std::string cpuFileName = std::string(dirName) + "/cpu.bin";

  std::ofstream cpuFile(cpuFileName, std::ios::binary | std::ios::trunc);
  if (!cpuFile) {
    compat::println(stderr, "CPU state save file cannot be opened: {}", cpuFileName);
    return INVALID_CPU_FILE;
  }

  auto cleanWrite = [&cpuFile](const void *buf, size_t count) -> bool {
    cpuFile.write(static_cast<const char *>(buf), static_cast<std::streamsize>(count));
    return static_cast<bool>(cpuFile);
  };

  CPUSnapshotPrefix snapshot{cpuState.areg, cpuState.breg, cpuState.creg,
                             cpuState.iptr, 0, cpuState.nWptr, {}};
  if (!cleanWrite(&snapshot, sizeof(snapshot))) {
    return INVALID_CPU_WRITE;
  }

  wptrEndAddr = static_cast<uint32_t>(cpuState.wptr + static_cast<uint32_t>(cpuState.nWptr) * BytesPerWord - 1U);
  if (!cleanWrite(&wptrEndAddr, sizeof(wptrEndAddr))) {
    return INVALID_CPU_WRITE;
  }

  if (!cleanWrite(&watch, sizeof(WATCH))) {
    return INVALID_CPU_WRITE;
  }

  RuntimeSnapshot runtime = captureRuntime();
  if (!cleanWrite(runtime.data(), sizeof(runtime))) {
    return INVALID_CPU_WRITE;
  }
  if (!externalChannels.empty()) {
    uint64_t totalBytes = 0;
    for (const auto &[address, endpoint] : externalChannels) {
      (void)address;
      totalBytes += endpoint.input.size() + endpoint.output.size();
    }
    if (totalBytes > 16777216) {
      return INVALID_CPU_WRITE;
    }
    std::array<uint32_t, 4> header{0x45584348U, 1, static_cast<uint32_t>(externalChannels.size()), static_cast<uint32_t>(totalBytes)};
    if (!cleanWrite(header.data(), sizeof(header))) {
      return INVALID_CPU_WRITE;
    }
    for (const auto &[address, endpoint] : externalChannels) {
      std::array<uint32_t, 12> record{address, endpoint.capacity, endpoint.waiter, endpoint.active,
          endpoint.length, endpoint.inputWait, endpoint.queued, endpoint.altOwner, endpoint.altWake,
          endpoint.altNotified, static_cast<uint32_t>(endpoint.input.size()), static_cast<uint32_t>(endpoint.output.size())};
      std::vector<uint8_t> bytes(endpoint.input.begin(), endpoint.input.end());
      bytes.insert(bytes.end(), endpoint.output.begin(), endpoint.output.end());
      if (!cleanWrite(record.data(), sizeof(record)) || !cleanWrite(bytes.data(), bytes.size())) {
        return INVALID_CPU_WRITE;
      }
    }
  }

  cpuFile.close();
  return cpuFile ? 0 : INVALID_CPU_WRITE;
}

uint32_t get_iptr(void) {
  return (cpuState.iptr);
}

int loadCPUState(const char *dirName) {
  if (dirName == nullptr || *dirName == '\0') {
    return INVALID_CPU_FILENAME;
  }

  std::string cpuFileName = std::string(dirName) + "/cpu.bin";

  std::ifstream cpuFile(cpuFileName, std::ios::binary);
  if (!cpuFile) {
    compat::println(stderr, "CPU state load file cannot be opened: {}", cpuFileName);
    return INVALID_CPU_FILE;
  }

  /* reads exactly count bytes */
  auto cleanRead = [&cpuFile](void *buf, size_t count) -> bool {
    cpuFile.read(static_cast<char *>(buf), static_cast<std::streamsize>(count));
    return cpuFile.gcount() == static_cast<std::streamsize>(count);
  };

  CPUSnapshotPrefix snapshot{};
  if (!cleanRead(&snapshot, sizeof(snapshot))) {
    return INVALID_CPU_READ;
  }

  uint32_t loadedEndAddress = 0;
  if (!cleanRead(&loadedEndAddress, sizeof(loadedEndAddress))) {
    return INVALID_CPU_READ;
  }

  WATCH loadedWatch{};
  if (!cleanRead(&loadedWatch, sizeof(loadedWatch))) {
    return INVALID_CPU_READ;
  }

  RuntimeSnapshot runtime{};
  if (snapshot.nWptr < 0 || snapshot.nWptr > MAX_WPTR ||
      !cleanRead(runtime.data(), sizeof(runtime)) || !validRuntime(runtime)) {
    return INVALID_CPU_FILE;
  }
  if (runtime[RuntimeStatus] & STATUS_INTERRUPTED_OPERATION_MASK) {
    return INVALID_CPU_FILE;
  }
  std::unordered_map<uint32_t, ExternalChannel> loadedChannels;
  {
    std::array<uint32_t, 4> header{};
    cpuFile.read(reinterpret_cast<char *>(header.data()), sizeof(header));
    std::streamsize headerBytes = cpuFile.gcount();
    if (headerBytes != 0) {
      if (headerBytes != static_cast<std::streamsize>(sizeof(header)) || header[0] != 0x45584348U || header[1] != 1 ||
          header[2] > 1024 || header[3] > 16777216) {
        return INVALID_CPU_FILE;
      }
      uint64_t total = 0;
      auto validDescriptor = [](uint32_t value) {
        return value == NO_PROCESS || (!(value & 2U) && (value & WordSelectMask) != NO_PROCESS);
      };
      for (uint32_t index = 0; index < header[2]; ++index) {
        std::array<uint32_t, 12> record{};
        if (!cleanRead(record.data(), sizeof(record))) {
          return INVALID_CPU_FILE;
        }
        uint64_t bytes = static_cast<uint64_t>(record[10]) + record[11];
        total += bytes;
        if ((record[0] & 3U) || loadedChannels.contains(record[0]) || record[1] == 0 || record[1] > 1048576 ||
            record[4] > record[1] || record[5] > 1 || record[6] > 1 || record[8] > 1 || record[9] > 1 ||
            record[10] > record[1] || record[11] > record[1] || total > header[3] ||
            !validDescriptor(record[2]) || !validDescriptor(record[3]) || !validDescriptor(record[7])) {
          return INVALID_CPU_FILE;
        }
        std::vector<uint8_t> data(static_cast<std::size_t>(bytes));
        if (!cleanRead(data.data(), data.size())) {
          return INVALID_CPU_FILE;
        }
        ExternalChannel endpoint{record[1], {}, {}};
        endpoint.waiter = record[2];
        endpoint.active = record[3];
        endpoint.length = record[4];
        endpoint.inputWait = record[5] != 0;
        endpoint.queued = record[6] != 0;
        endpoint.altOwner = record[7];
        endpoint.altWake = record[8] != 0;
        endpoint.altNotified = record[9] != 0;
        endpoint.input.insert(endpoint.input.end(), data.begin(), data.begin() + record[10]);
        endpoint.output.insert(endpoint.output.end(), data.begin() + record[10], data.end());
        loadedChannels.emplace(record[0], std::move(endpoint));
      }
      if (total != header[3]) {
        return INVALID_CPU_FILE;
      }
    }
  }
  resetScheduler();
  externalChannels = std::move(loadedChannels);
  wptrEndAddr = loadedEndAddress;
  watch = loadedWatch;
  cpuState = {snapshot.areg,
              snapshot.breg,
              snapshot.creg,
              snapshot.iptr, snapshot.nWptr};
  restoreRuntime(runtime);
  return 0;
}

int setWatch(const char *reg, const char *parm) {
  uint32_t value{0};

  bool enable = (strcmp(parm, "clear") != 0);

  if (enable) {
    sscanf(parm, "%" SCNx32, &value);
  }

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
    watch.nWptr = static_cast<int>(value);
    break;

  default:
    return BAD_WATCH_CONDITION;
    break;
  }

  return 0;
}

bool anyWatch() {
  return watch.watchAreg || watch.watchBreg || watch.watchCreg || watch.watchIptr ||
         watch.watchNWptr;
}

bool checkWatch() {
  return (watch.watchAreg && (cpuState.areg == watch.areg)) ||
         (watch.watchBreg && (cpuState.breg == watch.breg)) ||
         (watch.watchCreg && (cpuState.creg == watch.creg)) ||
         (watch.watchIptr && (cpuState.iptr == watch.iptr)) ||
         (watch.watchNWptr && (cpuState.nWptr == watch.nWptr));
}

/**************************
 * Print the values in the processor state
 */
int printCPUState() {
  int result = 0;
  uint32_t address = 0;
  uint32_t value = 0;
  compat::println("A=0x{:08x} B=0x{:08x} C=0x{:08x}  Iptr=0x{:08x}", cpuState.areg, cpuState.breg,
                  cpuState.creg, cpuState.iptr);
  compat::println("Wptr=0x{:08x} Priority={} State={} Status=0x{:08x}", cpuState.wptr, cpuState.priority,
                  cpuState.running ? "running" : "idle", cpuState.status);
  for (int index = 0; index < cpuState.nWptr; ++index) {
    if (index == 0) compat::println("Wptr");
    addrWptrWord(static_cast<uint32_t>(index), &address);
    result = readBytes(address, BytesPerWord, &value);
    compat::print(" {:02x}=0x{:08x}", index, value);
    if (index % WPTR_PRINT_COLS == WPTR_PRINT_COLS - 1) compat::println("    ");
  }
  compat::println("\n");
  return result;
}

int captureCPUTrace(CPUTrace &trace) {
  trace.state = cpuState;
  trace.code = instrCode;
  trace.operand = operand;
  trace.length = instrLength;
  std::copy_n(instrBytes, instrLength, trace.bytes.begin());
  trace.workspace.clear();
  trace.workspace.reserve(static_cast<size_t>(std::max(cpuState.nWptr, 0)));
  int result = 0;
  uint32_t address = 0;
  uint32_t value = 0;
  for (int index = 0; index < cpuState.nWptr; ++index) {
    addrWptrWord(static_cast<uint32_t>(index), &address);
    result = readBytes(address, BytesPerWord, &value);
    trace.workspace.push_back(value);
  }
  return result;
}

int printCPUState(const CPUTrace &trace) {
  const CPUSTATE &state = trace.state;
  compat::println("A=0x{:08x} B=0x{:08x} C=0x{:08x}  Iptr=0x{:08x}", state.areg, state.breg,
                  state.creg, state.iptr);
  compat::println("Wptr=0x{:08x} Priority={} State={} Status=0x{:08x}", state.wptr, state.priority,
                  state.running ? "running" : "idle", state.status);

  for (size_t index = 0; index < trace.workspace.size(); ++index) {
    if (index == 0) {
      compat::println("Wptr");
    }

    compat::print(" {:02x}=0x{:08x}", index, trace.workspace[index]);

    if (index % WPTR_PRINT_COLS == WPTR_PRINT_COLS - 1) {
      compat::println("    ");
    }
  }

  compat::println("\n");

  return 0;
}

/**************************
 * Print the values of the OMR
 */
int printOMRState() {
  // show the OMR Enables register
  compat::println("OTHER MACHINE REGISTERS");
  compat::println("-----------------------");
  compat::println("Enables=0x{:08x}", omrState.Enables);
  compat::println("TrapEnablesHP=0x{:04x} TrapEnablesLP=0x{:04x} Halted={}",
                  trapEnableMask(HIGH_PRIORITY), trapEnableMask(LOW_PRIORITY), omrState.halted);
  compat::print("ClockRegHP=0x{:08x} ", omrState.ClockRegHP);
  compat::print("ClockRegLP=0x{:08x} ", omrState.ClockRegLP);
  compat::println("ClockEnables=0x{:02x}", omrState.ClockEnables);
  compat::println("TimerFrontHP=0x{:08x} AlarmHP=0x{:08x}", omrState.TptrReg[0], omrState.TnextReg[0]);
  compat::println("TimerFrontLP=0x{:08x} AlarmLP=0x{:08x}", omrState.TptrReg[1], omrState.TnextReg[1]);
  compat::println("ReadyFrontHP=0x{:08x} ReadyFrontLP=0x{:08x}",
                  omrState.ProcQueueFPtr[0], omrState.ProcQueueFPtr[1]);
  compat::print("HP_ErrFlag=0x{:02x} ", omrState.HP_ErrorFlag);
  compat::print("LP_ErrFlag=0x{:02x} ", omrState.LP_ErrorFlag);
  compat::println("HaltOnError=0x{:02x}", omrState.HaltOnErrorFlag);
  return (0);
}
/**************************
 * Print the values in the Enables Register
 */
int printEnablesRegState() {

  compat::println("Enables Register Value=0x{:08x}", omrState.Enables);
  if (omrState.Enables & LP_PROCESS_INT_ENB)
    compat::println(" LP_PROCESS_INT_ENB	is set");
  if (omrState.Enables & LP_TIMESLICE_ENB)
    compat::println(" LP_TIMESLICE_ENB	is set");
  if (omrState.Enables & LP_EXTERNALEVENT_ENB)
    compat::println(" LP_EXTERNALEVENT_ENB	is set");
  if (omrState.Enables & LP_TIMER_ALRM_ENB)
    compat::println(" LP_TIMER_ALRM_ENB	is set");
  if (omrState.Enables & HP_PROCESS_INT_ENB)
    compat::println(" HP_PROCESS_INT_ENB	is set");
  if (omrState.Enables & HP_TIMESLICE_ENB)
    compat::println(" HP_TIMESLICE_ENB	is set");
  if (omrState.Enables & HP_EXTERNALEVENT_ENB)
    compat::println(" HP_EXTERNALEVENT_ENB	is set");
  if (omrState.Enables & HP_TIMER_ALRM_ENB)
    compat::println(" HP_TIMER_ALRM_ENB	is set");
  if (omrState.Enables & BREAKPOINT_TRAPENB)
    compat::println(" BREAKPOINT_TRAPENB	is set");
  if (omrState.Enables & INTEGER_ERR_TRAPENB)
    compat::println(" INTEGER_ERR_TRAPENB	is set");
  if (omrState.Enables & INTEGER_OVF_TRAPENB)
    compat::println(" INTEGER_OVF_TRAPENB	is set");
  if (omrState.Enables & ILL_OPCODE_TRAPENB)
    compat::println(" ILL_OPCODE_TRAPENB	is set");
  if (omrState.Enables & LOADTRAP_TRAPENB)
    compat::println(" LOADTRAP_TRAPENB	is set");
  if (omrState.Enables & STORETRAP_TRAPENB)
    compat::println(" STORETRAP_TRAPENB	is set");
  if (omrState.Enables & INTERNALCH_TRAPENB)
    compat::println(" INTERNALCH_TRAPENB	is set");
  if (omrState.Enables & EXTERNALCH_TRAPENB)
    compat::println(" EXTERNALCH_TRAPENB	is set");
  if (omrState.Enables & TIMER_TRAPENB)
    compat::println(" TIMER_TRAPENB	is set");
  if (omrState.Enables & TIMESLICE_TRAPENB)
    compat::println(" TIMESLICE_TRAPENB	is set");
  if (omrState.Enables & RUN_TRAPENB)
    compat::println(" RUN_TRAPENB	is set");
  if (omrState.Enables & SIGNAL_TRAPENB)
    compat::println(" SIGNAL_TRAPENB	is set");
  if (omrState.Enables & PROCESS_TRAPENB)
    compat::println(" PROCESS_TRAPENB	is set");
  if (omrState.Enables & QUEUE_EMPTY_TRAPENB)
    compat::println(" QUEUE_EMPTY_TRAPENB	is set");

  return (0);
}

/**************************
 * Get the bytes for the next instruction and determine what the instruction
 * code and operand are
 */
namespace {
/*
 * Decodes the instruction at address, accumulating its prefixes: pfix (2x) and nfix (6x) bytes
 * build the operand, and an opr (Fx) byte turns it into an extended instruction code. The
 * extended codes are offset by 0x10 so they don't clash with the direct ones, and lddevid
 * (which would clash with ldprodid) gets its own code. At most maxLength bytes are read, which
 * keeps a decode within one memory block.
 */
int decodeAt(uint32_t address, uint32_t maxLength, uint32_t &code, uint32_t &value, int &length,
             unsigned char *bytes, std::size_t capacity) {
  code = 0;
  value = 0;
  length = 0;
  for (;;) {
    if (static_cast<uint32_t>(length) >= maxLength) return READ_UNUSED_MEM;
    uint32_t cByte = 0;
    int result = readBytes(address + static_cast<uint32_t>(length), 1, &cByte);
    if (result) return result;
    if (static_cast<std::size_t>(length) < capacity) bytes[length] = static_cast<unsigned char>(cByte);
    ++length;
    value = (value << 4) + (cByte & 0x0F);
    code = (cByte & 0xF0) >> 4;
    if (code == 0x02) continue;
    if (code == 0x06) {
      value = ~value;
      continue;
    }
    if (code == 0x0F) code = value == 0x17C ? LDDEVID : (value & 0x1FF) + 0x10;
    return 0;
  }
}
}

int decodeNextInstr() {
  int result = 0;
  result = prepareProcess();
  if (result) return result;

  if (cpuState.status & STATUS_INTERRUPTED_OPERATION_MASK) return INTERRUPTED_OPERATION_UNSUPPORTED;

  uint32_t block = cpuState.iptr >> BLKSHIFT;
  DecodedInstr &cached = decodeCache[cpuState.iptr & (decodeCacheSize - 1)];
  if (cached.length && cached.iptr == cpuState.iptr && cached.version == blockVersion[block]) {
    instrCode = cached.code;
    operand = cached.operand;
    instrLength = cached.length;
    std::memcpy(instrBytes, cached.bytes, sizeof cached.bytes);
    return 0;
  }

  result = decodeAt(cpuState.iptr, UINT32_MAX, instrCode, operand, instrLength, instrBytes, sizeof instrBytes);
  if (result) {
    compat::println("{}", memoryError(result));
    compat::println("Error occurred when reading instruction at {:08x}, offset {:02x}",
                    cpuState.iptr, instrLength);
    return (-1);
  }

  uint32_t last = cpuState.iptr + static_cast<uint32_t>(instrLength) - 1U;
  if (static_cast<std::size_t>(instrLength) <= sizeof cached.bytes && last >> BLKSHIFT == block &&
      !isMemoryDeviceAddress(cpuState.iptr)) {
    cached = {cpuState.iptr, blockVersion[block], instrCode, operand, instrLength, {}};
    std::memcpy(cached.bytes, instrBytes, sizeof cached.bytes);
  }

  return (0);
}

/**************************
 * Print out the address, bytes and decoded instruction to be executed next
 */
int printNextInstr() {
  CPUTrace trace;
  trace.state = cpuState;
  trace.code = instrCode;
  trace.operand = operand;
  trace.length = instrLength;
  std::copy_n(instrBytes, instrLength, trace.bytes.begin());
  return printNextInstr(trace);
}

int printNextInstr(const CPUTrace &trace) {
  char operandCh[100];

  compat::print("{:08x}  ", trace.state.iptr);

  for (int index = 0; index < trace.length; ++index) {
    compat::print("{:02x} ", trace.bytes[static_cast<size_t>(index)]);
  }

  /*
   * for the jump, conditional jump and call instructions, we need to print
   * the address (i.e. iptr + operand) rather than the operand
   */
  if (trace.code == 0x00 || trace.code == 0x0A) {
    snprintf(operandCh, sizeof(operandCh), " loc_%08" PRIx32,
            (trace.state.iptr + static_cast<uint32_t>(trace.length) +
             trace.operand) &
                0xFFFFFFFF);
  } else if (trace.code == 0x09) {
    snprintf(operandCh, sizeof(operandCh), " sub_%08" PRIx32,
            (trace.state.iptr + static_cast<uint32_t>(trace.length) +
             trace.operand) &
                0xFFFFFFFF);
  } else if (trace.code > 0x0F) {
    operandCh[0] = '\0';
  } else {
    snprintf(operandCh, sizeof(operandCh), " %-8" PRIx32, trace.operand);
  }

  compat::println(" {}{}", instrEntry[trace.code].mnemonic, operandCh);

  return (0);
}

void requestPeripheralPoll() {
  peripheralPollRequested = true;
}

uint64_t elapsedCpuCycles() {
  return cpuCycles;
}

namespace {
/* cpuCycles at which the next tick of an enabled clock, or of the timeslice, falls */
uint64_t nextTick() {
  auto cyclesToTick = [](uint64_t phase, uint64_t frequency) { return (CPU_CLOCK - phase + frequency - 1) / frequency; };
  uint64_t cycles = omrState.halted ? UINT64_MAX : cyclesToTick(omrState.TimeslicePhase, 1000);
  if (omrState.ClockEnables & HPTIMER_MASK) cycles = std::min(cycles, cyclesToTick(omrState.ClockPhaseHP, HP_TIMER_HZ));
  if (omrState.ClockEnables & LPTIMER_MASK) cycles = std::min(cycles, cyclesToTick(omrState.ClockPhaseLP, LP_TIMER_HZ));
  return cycles == UINT64_MAX ? UINT64_MAX : clockCycles + cycles;
}

/* brings the clocks and the timeslice up to cpuCycles; the clock ticks are returned so the
 * caller can release the timers they made due */
int applyClockCycles(uint64_t &ticksHP, uint64_t &ticksLP) {
  uint64_t cycles = cpuCycles - clockCycles;
  clockCycles = cpuCycles;
  auto advanceClock = [cycles](uint32_t &clock, uint64_t &phase, uint32_t frequency) -> uint64_t {
    /* most syncs don't reach the next tick: skip the divisions */
    if (cycles < CPU_CLOCK && phase + cycles * frequency < CPU_CLOCK) {
      phase += cycles * frequency;
      return 0;
    }
    uint64_t elapsed = phase + (cycles % CPU_CLOCK) * frequency;
    uint64_t ticks = (cycles / CPU_CLOCK) * frequency + elapsed / CPU_CLOCK;
    phase = elapsed % CPU_CLOCK;
    clock += static_cast<uint32_t>(ticks);
    return ticks;
  };
  if (omrState.ClockEnables & HPTIMER_MASK)
    ticksHP = advanceClock(omrState.ClockRegHP, omrState.ClockPhaseHP, HP_TIMER_HZ);
  if (omrState.ClockEnables & LPTIMER_MASK)
    ticksLP = advanceClock(omrState.ClockRegLP, omrState.ClockPhaseLP, LP_TIMER_HZ);
  int result = 0;
  if (omrState.halted) {
    result = CPU_HALTED;
  } else {
    uint32_t unusedClock = 0;
    uint64_t sliceTicks = advanceClock(unusedClock, omrState.TimeslicePhase, 1000);
    auto ageQuantum = [sliceTicks](CPUSTATE &state) {
      if (state.running && state.priority == LOW_PRIORITY)
        state.timeslicePeriods = static_cast<uint32_t>(std::min<uint64_t>(2, state.timeslicePeriods + sliceTicks));
    };
    if (sliceTicks) {
      ageQuantum(cpuState);
      if (lowPriorityShadowValid) ageQuantum(lowPriorityShadow);
      for (TrapFrame &frame : trapFrames) ageQuantum(frame.caller);
    }
  }
  nextClockTick = nextTick();
  return result;
}

/* applies the cycles not yet seen by the clocks, and releases the timers that became due */
int syncClocks() {
  uint32_t oldHP = omrState.ClockRegHP;
  uint32_t oldLP = omrState.ClockRegLP;
  uint64_t ticksHP = 0;
  uint64_t ticksLP = 0;
  int result = applyClockCycles(ticksHP, ticksLP);
  return result ? result : releaseTimers(oldHP, oldLP, ticksHP, ticksLP);
}

/* the scheduler state in which pollTimers has nothing to do, so instructions that leave it
 * alone may skip it: a process runs, nothing is pending or due, and it can't be preempted */
bool schedulerQuiet() {
  if (!fastPathEnabled || omrState.halted || !cpuState.running ||
      (cpuState.status & STATUS_INTERRUPTED_OPERATION_MASK) || pendingTimerTrap.cause ||
      !externalChannels.empty())
    return false;
  bool preemptable = !trapHandlerActive() && !interruptHandlerActive() && cpuState.priority == LOW_PRIORITY &&
                     omrState.ProcQueueFPtr[HIGH_PRIORITY] != NO_PROCESS && !cpuState.interruptsDisabled &&
                     (omrState.Enables & LP_PROCESS_INT_ENB);
  return !preemptable && !timersDue(omrState.ClockRegHP, omrState.ClockRegLP, 0, 0);
}

/* rechecks after an instruction that went through pollTimers */
void updateQuiescence(int result) {
  quiescent = result == 0 && schedulerQuiet();
}

bool peripheralPollDue() {
  return peripheralPoller && (peripheralPollRequested || cpuCycles - lastPeripheralPoll >= peripheralPollCycles);
}

/* instructions that neither look at nor change the timers or the scheduler, other than by
 * raising a trap or timeslicing (requestTrap and timesliceProcess end quiescence) */
bool leavesSchedulerAlone(uint32_t code) {
  static constexpr std::string_view pure[] = {
      "adc", "add", "ajw", "and", "bcnt", "bitcnt", "bitrevnbits", "bitrevword", "bsub", "call", "cb",
      "cbu", "ccnt1", "cir", "ciru", "cj", "crcbyte", "crcword", "cs", "csngl", "csu", "csub0", "cword",
      "devlb", "devls", "devlw", "devsb", "devss", "devsw", "diff", "div", "dup", "eqc", "fmul", "gajw",
      "gcall", "gt", "gtu", "j", "ladd", "lb", "lbx", "ldc", "ldclock", "lddevid", "ldiff", "ldiv", "ldl",
      "ldlp", "ldmemstartval", "ldnl", "ldnlp", "ldpi", "ldpri", "ldprodid", "ldtimer", "lend", "lmul",
      "ls", "lshl", "lshr", "lsub", "lsum", "lsx", "mint", "mul", "nop", "norm", "not", "or", "pop",
      "prod", "rem", "ret", "rev", "satadd", "satmul", "satsub", "sb", "shl", "shr", "slmul", "ss",
      "ssub", "stl", "stnl", "sub", "sulmul", "sum", "testerr", "wcnt", "wsub", "wsubdb", "xbword",
      "xdble", "xor", "xsword", "xword"};
  return std::ranges::find(pure, std::string_view(instrEntry[code].mnemonic)) != std::end(pure);
}

bool endsBlock(uint32_t code) {
  std::string_view mnemonic = instrEntry[code].mnemonic;
  return mnemonic == "j" || mnemonic == "call" || mnemonic == "gcall" || mnemonic == "ret" || mnemonic == "lend";
}

/* the decoded run of instructions starting at iptr, or nullptr when there is none to run
 * (device memory, or the first instruction can't be decoded) */
const CodeBlock *codeBlock(uint32_t iptr) {
  uint32_t memoryBlock = iptr >> BLKSHIFT;
  CodeBlock &block = codeBlocks[(iptr ^ iptr >> 12) & (codeBlockCacheSize - 1)];
  if (block.count && block.iptr == iptr && block.version == blockVersion[memoryBlock]) return &block;
  block.count = 0;
  if (isMemoryDeviceAddress(iptr)) return nullptr;
  block.iptr = iptr;
  block.version = blockVersion[memoryBlock];
  uint32_t address = iptr;
  while (block.count < static_cast<int>(std::size(block.instrs))) {
    uint32_t code = 0;
    uint32_t value = 0;
    int length = 0;
    unsigned char bytes[16];
    if (decodeAt(address, BLKSIZE - (address & ADDR_IN_BLK_MASK), code, value, length, bytes, sizeof bytes)) break;
    /* the classification compares mnemonics: do it once per instruction code */
    static std::array<int8_t, 0x201> pureCodes = [] {
      std::array<int8_t, 0x201> codes{};
      for (uint32_t index = 0; index < codes.size(); ++index)
        codes[index] = static_cast<int8_t>(leavesSchedulerAlone(index) | endsBlock(index) << 1);
      return codes;
    }();
    int8_t kind = code < pureCodes.size() ? pureCodes[code]
                                          : static_cast<int8_t>(leavesSchedulerAlone(code) | endsBlock(code) << 1);
    bool pure = kind & 1;
    block.instrs[block.count++] = {instrEntry[code].function, value, code, instrEntry[code].cpucycles,
                                   static_cast<uint32_t>(length), pure};
    address += static_cast<uint32_t>(length);
    if (!pure || (kind & 2) || (address & ADDR_IN_BLK_MASK) == 0) break;
  }
  return block.count ? &block : nullptr;
}

/*
 * execInstr for a block instruction while the scheduler is quiet. It does what execInstr does,
 * but the clocks are only synced when a tick is due or the instruction isn't pure, and
 * pollTimers only runs when a peripheral poll is due, the instruction isn't pure, or the
 * instruction ended quiescence: otherwise both would have found nothing to do.
 */
int execBlockInstr(const BlockInstr &instr, int *breakFlag) {
  uint32_t instructionAddress = cpuState.iptr;
  instrCode = instr.code;
  operand = instr.operand;
  instrLength = static_cast<int>(instr.length);
  cpuState.iptr = instructionAddress + instr.length;

  executingInstruction = true;
  cpuCycles += instr.cycles;
  int result = 0;
  if (!instr.pure || cpuCycles >= nextClockTick) result = syncClocks();
  if (!result) result = instr.function(instr.operand);
  else cpuState.iptr = instructionAddress;
  executingInstruction = false;
  if (result) {
    quiescent = false;
  } else if (!instr.pure || !quiescent || peripheralPollDue()) {
    result = pollTimers();
    updateQuiescence(result);
  }
  freshPoll = result == 0;

  *breakFlag = checkWatch();

  return result;
}
}

int advanceTimers(uint64_t cycles) {
  cpuCycles += cycles;
  int result = syncClocks();
  return result || executingInstruction ? result : pollTimers();
}

void setFastPath(bool enabled) {
  fastPathEnabled = enabled;
  quiescent = false;
}

int execInstr(int *breakFlag) {
  int result = 0;
  if (omrState.halted) {
    *breakFlag = 0;
    return CPU_HALTED;
  }
  if (!cpuState.running) {
    result = decodeNextInstr();
    if (result) {
      *breakFlag = 0;
      return result;
    }
  }

  uint32_t instructionAddress = cpuState.iptr;
  if (cpuState.status & STATUS_INTERRUPTED_OPERATION_MASK) return INTERRUPTED_OPERATION_UNSUPPORTED;
  cpuState.iptr = instructionAddress + static_cast<uint32_t>(instrLength);

  executingInstruction = true;
  result = advanceTimers(instrEntry[instrCode].cpucycles);
  if (!result) result = instrEntry[instrCode].function(operand);
  else cpuState.iptr = instructionAddress;
  executingInstruction = false;
  if (!result) result = pollTimers();
  freshPoll = result == 0;
  updateQuiescence(result);

  *breakFlag = checkWatch();

  return (result);
}

/*
 * Runs exec+decode pairs. While the scheduler is quiet, decoded blocks run instead (see
 * execBlockInstr), and once they stop the next instruction is decoded for the caller as a
 * pair would have. A run that stops on an instruction reports it as executed but not completed.
 */
RunResult runInstructions(uint64_t count) {
  RunResult run;
  auto stop = [&run](int result, int watchTripped) {
    run.stopped = true;
    run.execResult = result;
    run.watchTripped = watchTripped;
    return run;
  };
  bool noBlock = false;
  while (run.completed < count) {
    if (!quiescent || noBlock) {
      noBlock = false;
      int watchTripped = 0;
      int result = execInstr(&watchTripped);
      if (result || watchTripped || decodeNextInstr()) return stop(result, watchTripped);
      ++run.completed;
      continue;
    }
    bool executed = false;
    while (quiescent && run.completed < count) {
      const CodeBlock *block = codeBlock(cpuState.iptr);
      if (!block) {
        noBlock = true;
        break;
      }
      uint32_t memoryBlock = block->iptr >> BLKSHIFT;
      uint32_t next = block->iptr;
      for (int index = 0; index < block->count; ++index) {
        const BlockInstr &instr = block->instrs[index];
        int watchTripped = 0;
        int result = execBlockInstr(instr, &watchTripped);
        if (result || watchTripped) {
          uint64_t ticksHP = 0;
          uint64_t ticksLP = 0;
          applyClockCycles(ticksHP, ticksLP);
          return stop(result, watchTripped);
        }
        executed = true;
        ++run.completed;
        next += instr.length;
        if (run.completed == count || !quiescent || cpuState.iptr != next ||
            blockVersion[memoryBlock] != block->version)
          break;
      }
    }
    uint64_t ticksHP = 0;
    uint64_t ticksLP = 0;
    applyClockCycles(ticksHP, ticksLP);
    if (executed && decodeNextInstr()) {
      --run.completed;
      return stop(0, 0);
    }
  }
  return run;
}

int readWptrWord(uint32_t index, uint32_t *value) {
  int result = 0;
  uint32_t address;

  addrWptrWord(index, &address);

  result = readBytes(address, BytesPerWord, value);

  return result;
}

int addrWptrWord(uint32_t index, uint32_t *address) {
  *address = cpuState.wptr + index * BytesPerWord;

  return 0;
}

int storeWptrWord(uint32_t index, uint32_t value) {
  int result = 0;
  uint32_t address;

  addrWptrWord(index, &address);
  result = storeBytes(address, BytesPerWord, value);

  return (result);
}

int wptrPopState() {
  int result = 0;

  result = readWptrWord(0, &cpuState.iptr);
  if (result) return result;

  if ((result = allocWptr(4U))) {
    return result;
  }

  return result;
}

int wptrPushState() {
  int result = 0;

  if ((result = allocWptr(static_cast<uint32_t>(-4)))) {
    return result;
  }

  storeWptrWord(3, cpuState.creg);
  storeWptrWord(2, cpuState.breg);
  storeWptrWord(1, cpuState.areg);
  storeWptrWord(0, cpuState.iptr);

  return result;
}

int allocWptr(uint32_t adjustment) {
  int64_t count = static_cast<int32_t>(adjustment);
  cpuState.wptr += adjustment * BytesPerWord;
  if (count < 0) {
    uint64_t words = uint64_t{0} - static_cast<uint64_t>(count);
    cpuState.nWptr = words >= static_cast<uint64_t>(MAX_WPTR - cpuState.nWptr)
                        ? MAX_WPTR : cpuState.nWptr + static_cast<int>(words);
  } else {
    cpuState.nWptr = count >= cpuState.nWptr ? 0 : cpuState.nWptr - static_cast<int>(count);
  }
  return 0;
}

int setWptr(uint32_t value) {
  cpuState.wptr = value;
  return 0;
}

uint32_t pop(void) {
  uint32_t holdValue = cpuState.areg;
  cpuState.areg = cpuState.breg;
  cpuState.breg = cpuState.creg;
  cpuState.creg = undefinedWord;

  return holdValue;
}

int push(uint32_t value) {
  cpuState.creg = cpuState.breg;
  cpuState.breg = cpuState.areg;
  cpuState.areg = value;

  return 0;
}

int setAreg(uint32_t value) {
  cpuState.areg = value;
  return (0);
}

int setBreg(uint32_t value) {
  cpuState.breg = value;
  return (0);
}

int setCreg(uint32_t value) {
  cpuState.creg = value;
  return (0);
}

int setIptr(uint32_t value) {
  cpuState.iptr = value;
  return (0);
}

const char *st20Error(int error) {
  switch (error) {
  case BAD_WPTR:
    return "Invalid Wptr word referenced";

  case WPTR_UNUSED:
    return "An uninitialized Wptr word was accessed";

  case WPTR_UNDERFLOW:
    return "Too many Wptr words deallocated";

  case WPTR_OVERFLOW:
    return "Ran out of room for more Wptr words";

  case BAD_WATCH_CONDITION:
    return "Parameters to the watch condition were invalid";

  case CPU_IDLE:
    return "No runnable process or enabled timer alarm";
  case TIMER_TRAP_UNSUPPORTED:
    return "Timer trap delivery is not implemented";
  case INVALID_PROCESS_LIST:
    return "Invalid or cyclic process list";
  case RUN_TRAP_UNSUPPORTED:
    return "Run trap delivery is not implemented";
  case SIGNAL_TRAP_UNSUPPORTED:
    return "Signal trap delivery is not implemented";
  case CHANNEL_HARDWARE_UNSUPPORTED:
    return "Hardware or core-reserved channel access is not implemented";
  case CHANNEL_TRAP_UNSUPPORTED:
    return "Internal channel trap delivery is not implemented";
  case INVALID_CHANNEL:
    return "Invalid internal channel descriptor";
  case INVALID_TRAP_HANDLER:
    return "Trap handler descriptor is missing or invalid";
  case TRAP_REENTRY:
    return "Trap handler reentered its own group";
  case INVALID_TRAP_RETURN:
    return "Invalid trap return context";
  case TRAP_HANDLER_BLOCKED:
    return "Trap handlers must not deschedule";
  case CPU_HALTED:
    return "Processor halted on arithmetic error";
  case INVALID_INTERRUPT_RETURN:
    return "Invalid external interrupt return context";
  case INTERRUPTED_OPERATION_UNSUPPORTED:
    return "Interrupted microcode operation cannot yet be resumed";
  case INVALID_SHADOW_ACCESS:
    return "Invalid or unsupported shadow register bank access";
  case SHADOW_BLOCK_REGISTERS_UNSUPPORTED:
    return "Shadow block-move register layout is not implemented";
  case INVALID_EXTERNAL_INTERRUPT:
    return "Invalid external interrupt request";
  default:
    return "Unknown st20 error";
  }
}

void resetTimers() {
  quiescent = false;
  omrState.ClockRegHP = timerGuess;
  omrState.ClockRegLP = timerGuess;
  omrState.ClockPhaseHP = 0;
  omrState.ClockPhaseLP = 0;
  omrState.ClockEnables |= HPTIMER_MASK; // set bit0
  omrState.ClockEnables |= LPTIMER_MASK; // set bit1
  omrState.Enables = 0xffffc000;         // the initial state, see omr.h
                                         // bit 14 and 15 are reserved but set to 1
}

int initTimer() {
  resetTimers();
  compat::println("CPU_CLOCK={:d} Hz\n HPTimer={:d} Hz\n LPTimer={:d} Hz",
                  CPU_CLOCK, HP_TIMER_HZ, LP_TIMER_HZ);

  return (0);
}
