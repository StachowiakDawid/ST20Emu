/********************************************************************/
/* 16/12/2011	-Fixed 'bitcnt' instruction							*/
/*				-Added some detailed instruction comment			*/
/* 19/12/2011   -Added 'db' (query database) command				*/
/* 20/12/2011	-Added 'vra' (verbose register access) command		*/
/*				 that show info's also in 'go' mode					*/
/*				 (added a status flag into struct cmdState_struct)	*/
/*				-fixed some C4996 warnings about float->uint32_t and	*/
/*				 uint32_t->float. A cast has been enough.				*/
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

#include "st20.h"
#include <cinttypes>
#include "instrentry.h"
#include "omr.h"

#include "../../common/compat.h"
#include "../../common/defines.h"
#include "../memory/memory.h"

// posix / linux
#include <fcntl.h>
#include <unistd.h>

// std
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <array>
#include <unordered_set>
#include <unordered_map>
#include <deque>
#include <bit>
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
bool portableInstructionAbi = false;
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
struct ExternalInterruptRequest {
  bool pending{false};
  uint32_t workspace{0};
  uint32_t iptr{0};
  uint32_t status{0};
};
struct ExternalInterruptFrame {
  uint32_t level;
  bool replacedShadow;
  bool previousShadowValid;
  uint32_t trapDepth;
  CPUSTATE caller;
  CPUSTATE previousShadow;
};
std::array<ExternalInterruptRequest, 8> externalInterruptRequests;
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
bool queueEmptyReported[2]{};

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
  RuntimeMoveData = RuntimeTrapTimeslicePeriods + 8,
  RuntimeHighShadowData = RuntimeMoveData + 20,
  RuntimeExternalInterruptData = RuntimeHighShadowData + 17,
  RuntimeFieldCount = RuntimeExternalInterruptData + 321
};
using RuntimeSnapshot = std::array<uint32_t, RuntimeFieldCount>;
constexpr uint32_t runtimeSnapshotMagic = 0x53543230U;
constexpr uint32_t runtimeSnapshotVersion = 11;

std::array<uint32_t, 16> encodeCpu(const CPUSTATE &state) {
  return {state.areg, state.breg,
      state.creg, state.iptr,
      static_cast<uint32_t>(state.nWptr), state.priority, state.wptr, state.running,
      state.move2dBlockLength, state.move2dDestStride, state.move2dSourceStride, state.status,
      state.interruptsDisabled, state.timeslicePeriods, state.moveRowWidth, state.moveBufferedWord};
}

CPUSTATE decodeCpu(const uint32_t *words) {
  return {words[0], words[1], words[2],
      words[3], static_cast<int>(words[4]), static_cast<unsigned char>(words[5]),
      words[6], words[7] != 0, words[8], words[9], words[10], words[11], words[12] != 0,
      words[13], words[14], words[15]};
}

bool validCpuWords(const uint32_t *words) {
  return words[4] <= MAX_WPTR && words[5] <= LOW_PRIORITY && words[7] <= 1 && words[12] <= 1 &&
         words[13] <= 2 && (!words[7] || !(words[6] & 3U)) && validPendingOperation(decodeCpu(words));
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
  snapshot[RuntimeMoveData] = cpuState.moveRowWidth;
  snapshot[RuntimeMoveData + 1] = cpuState.moveBufferedWord;
  snapshot[RuntimeMoveData + 2] = lowPriorityShadow.moveRowWidth;
  snapshot[RuntimeMoveData + 3] = lowPriorityShadow.moveBufferedWord;
  snapshot[RuntimeHighShadowData] = highPriorityShadowValid;
  auto highWords = encodeCpu(highPriorityShadow);
  std::copy(highWords.begin(), highWords.end(), snapshot.begin() + RuntimeHighShadowData + 1);
  for (std::size_t index = 0; index < 8; ++index) {
    const auto &request = externalInterruptRequests[index];
    std::size_t offset = RuntimeExternalInterruptData + index * 4;
    snapshot[offset] = request.pending;
    snapshot[offset + 1] = request.workspace;
    snapshot[offset + 2] = request.iptr;
    snapshot[offset + 3] = request.status;
  }
  snapshot[RuntimeExternalInterruptData + 32] = static_cast<uint32_t>(externalInterruptFrames.size());
  for (std::size_t index = 0; index < externalInterruptFrames.size(); ++index) {
    const auto &frame = externalInterruptFrames[index];
    std::size_t offset = RuntimeExternalInterruptData + 33 + index * 36;
    snapshot[offset] = frame.level;
    snapshot[offset + 1] = frame.replacedShadow;
    snapshot[offset + 2] = frame.previousShadowValid;
    snapshot[offset + 3] = frame.trapDepth;
    auto callerWords = encodeCpu(frame.caller);
    auto shadowWords = encodeCpu(frame.previousShadow);
    std::copy(callerWords.begin(), callerWords.end(), snapshot.begin() + static_cast<std::ptrdiff_t>(offset + 4));
    std::copy(shadowWords.begin(), shadowWords.end(), snapshot.begin() + static_cast<std::ptrdiff_t>(offset + 20));
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
    snapshot[RuntimeMoveData + 4 + index * 2] = frame.caller.moveRowWidth;
    snapshot[RuntimeMoveData + 5 + index * 2] = frame.caller.moveBufferedWord;
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
  {
    CPUSTATE shadow{snapshot[RuntimeShadowA], snapshot[RuntimeShadowB],
        snapshot[RuntimeShadowC], snapshot[RuntimeShadowIptr],
        static_cast<int>(snapshot[RuntimeShadowWords]), static_cast<unsigned char>(snapshot[RuntimeShadowPriority]),
        snapshot[RuntimeShadowWptr], snapshot[RuntimeShadowRunning] != 0, snapshot[RuntimeShadowMove2dRows],
        snapshot[RuntimeShadowMove2dDestStride], snapshot[RuntimeShadowMove2dSourceStride], snapshot[RuntimeShadowStatus],
        snapshot[RuntimeShadowInterruptsDisabled] != 0, snapshot[RuntimeShadowTimeslicePeriods],
        snapshot[RuntimeMoveData + 2], snapshot[RuntimeMoveData + 3]};
    if (!validPendingOperation(shadow)) return false;
    for (std::size_t index = 0; index < snapshot[RuntimeTrapDepth]; ++index) {
      std::size_t offset = RuntimeTrapFrameData + index * 15;
      CPUSTATE caller{snapshot[offset + 3], snapshot[offset + 4],
          snapshot[offset + 5], snapshot[offset + 6],
          static_cast<int>(snapshot[offset + 7]), static_cast<unsigned char>(snapshot[offset + 8]),
          snapshot[offset + 9], snapshot[offset + 10] != 0, snapshot[offset + 11], snapshot[offset + 12],
          snapshot[offset + 13], snapshot[offset + 14], snapshot[RuntimeTrapInterruptsDisabled + index] != 0,
          snapshot[RuntimeTrapTimeslicePeriods + index], snapshot[RuntimeMoveData + 4 + index * 2],
          snapshot[RuntimeMoveData + 5 + index * 2]};
      if (!validPendingOperation(caller)) return false;
    }
  }
    if (snapshot[RuntimeHighShadowData] > 1 ||
      !validCpuWords(snapshot.data() + RuntimeHighShadowData + 1)) return false;
    {
    uint32_t depth = snapshot[RuntimeExternalInterruptData + 32];
    if (depth > 8) return false;
    for (std::size_t index = 0; index < 8; ++index) {
      std::size_t offset = RuntimeExternalInterruptData + index * 4;
      if (snapshot[offset] > 1 || (snapshot[offset] &&
          (snapshot[offset + 1] == 0 || (snapshot[offset + 1] & 3U) ||
           (snapshot[offset + 3] & STATUS_INTERRUPTED_OPERATION_MASK)))) return false;
    }
    uint32_t lastLevel = 0;
    for (std::size_t index = 0; index < depth; ++index) {
      std::size_t offset = RuntimeExternalInterruptData + 33 + index * 36;
      if (snapshot[offset] >= 8 || (index != 0 && snapshot[offset] <= lastLevel) ||
          snapshot[offset + 1] > 1 || snapshot[offset + 2] > 1 || snapshot[offset + 3] > snapshot[RuntimeTrapDepth] ||
          !validCpuWords(snapshot.data() + offset + 4) || !validCpuWords(snapshot.data() + offset + 20)) return false;
      lastLevel = snapshot[offset];
    }
  }
  return true;
}

void restoreRuntime(const RuntimeSnapshot &snapshot) {
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
  cpuState.moveRowWidth = snapshot[RuntimeMoveData];
  cpuState.moveBufferedWord = snapshot[RuntimeMoveData + 1];
  highPriorityShadowValid = snapshot[RuntimeHighShadowData] != 0;
  highPriorityShadow = decodeCpu(snapshot.data() + RuntimeHighShadowData + 1);
  externalInterruptFrames.clear();
  externalInterruptRequests = {};
  {
    for (std::size_t index = 0; index < 8; ++index) {
      std::size_t offset = RuntimeExternalInterruptData + index * 4;
      externalInterruptRequests[index] = {snapshot[offset] != 0, snapshot[offset + 1], snapshot[offset + 2], snapshot[offset + 3]};
    }
    for (std::size_t index = 0; index < snapshot[RuntimeExternalInterruptData + 32]; ++index) {
      std::size_t offset = RuntimeExternalInterruptData + 33 + index * 36;
      externalInterruptFrames.push_back({snapshot[offset], snapshot[offset + 1] != 0, snapshot[offset + 2] != 0,
          snapshot[offset + 3], decodeCpu(snapshot.data() + offset + 4), decodeCpu(snapshot.data() + offset + 20)});
    }
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
  lowPriorityShadow.moveRowWidth = snapshot[RuntimeMoveData + 2];
  lowPriorityShadow.moveBufferedWord = snapshot[RuntimeMoveData + 3];
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
    caller.moveRowWidth = snapshot[RuntimeMoveData + 4 + index * 2];
    caller.moveBufferedWord = snapshot[RuntimeMoveData + 5 + index * 2];
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
        if (!portableInstructionAbiEnabled() && endpoint.length != 0) {
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

int prepareExternalContinuation(CPUSTATE &state, bool consume) {
  uint32_t descriptor = state.wptr | state.priority;
  for (auto &[address, endpoint] : externalChannels) {
    if (endpoint.waiter != descriptor || !endpoint.queued) continue;
    if (consume) {
      endpoint.waiter = NO_PROCESS;
      endpoint.queued = false;
      if (!portableInstructionAbiEnabled()) endpoint.active = NO_PROCESS;
      return 0;
    }
    if (!portableInstructionAbiEnabled()) return endpoint.length == 0 ? 0 : INVALID_CHANNEL;
    uint32_t buffer = 0;
    int result = readProcessWord(state.wptr, pw::Pointer, buffer);
    if (result) return result;
    state.areg = endpoint.length;
    state.breg = endpoint.inputWait ? buffer : address;
    state.creg = endpoint.inputWait ? address : buffer;
    state.moveRowWidth = endpoint.length;
    state.move2dBlockLength = 1;
    state.move2dDestStride = address;
    state.move2dSourceStride = NO_PROCESS;
    state.moveBufferedWord = 0;
    state.status |= STATUS_PORTABLE_ABI | ((endpoint.inputWait ? 6U : 7U) << 26);
    return 0;
  }
  return 0;
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
  if (!portableInstructionAbiEnabled()) return transferNativeExternalBytes(endpoint, buffer, length, input);
  endpoint.active = descriptor;
  cpuState.move2dDestStride = address;
  cpuState.move2dSourceStride = NO_PROCESS;
  return beginResumableMove(input ? 6U : 7U, length, input ? buffer : address, input ? address : buffer);
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
  result = prepareExternalContinuation(nextState, false);
  if (result) return result;
  if (cpuState.running) {
    lowPriorityShadow = cpuState;
    lowPriorityShadowValid = true;
  }
  selectPriority(priority);
  omrState.ProcQueueFPtr[priority] = next;
  if (next == NO_PROCESS) omrState.ProcQueueBPtr[priority] = NO_PROCESS;
  cpuState = nextState;
  prepareExternalContinuation(cpuState, true);
  queueEmptyReported[priority] = false;
  if (interrupted && (trapEnableMask(priority) & PROCESS_TRAPENB))
    return requestTrap(PROCESS_TRAPENB, priority, false, workspace | priority);
  return 0;
}

int releaseTimers(uint32_t oldHP, uint32_t oldLP, uint64_t ticksHP, uint64_t ticksLP) {
  if (pendingTimerTrap.cause) return 0;
  for (unsigned char priority : {static_cast<unsigned char>(HIGH_PRIORITY),
                                 static_cast<unsigned char>(LOW_PRIORITY)}) {
    uint32_t enable = priority == HIGH_PRIORITY ? HP_TIMER_ALRM_ENB : LP_TIMER_ALRM_ENB;
    if (!(omrState.Enables & enable)) continue;
    uint32_t oldClock = priority == HIGH_PRIORITY ? oldHP : oldLP;
    uint64_t ticks = priority == HIGH_PRIORITY ? ticksHP : ticksLP;
    std::unordered_set<uint32_t> visited;
    while (omrState.TptrReg[priority] != NO_PROCESS) {
      uint32_t workspace = omrState.TptrReg[priority];
      if ((workspace & 3U) || !visited.insert(workspace).second) return INVALID_PROCESS_LIST;
      uint32_t time = 0;
      int result = readProcessWord(workspace, pw::Time, time);
      if (result) return result;
      if (!timeReached(oldClock, time) && ticks < time - oldClock) break;
      uint32_t state = 0;
      int stateResult = readProcessWord(workspace, pw::State, state);
      bool alternative = stateResult == 0 && (state == ALT_WAITING || state == ALT_READY);
      bool needsWakeup = !alternative || state == ALT_WAITING;
      bool trapped = needsWakeup && (trapEnableMask(priority) & TIMER_TRAPENB) != 0;
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

int prepareProcess() {
  if (omrState.halted) return CPU_HALTED;
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
  if (soonest == UINT64_MAX) return CPU_IDLE;
  result = advanceTimers(soonest);
  return result ? result : cpuState.running ? 0 : CPU_IDLE;
}
}

std::size_t cpuRuntimeSnapshotSize() {
  return sizeof(RuntimeSnapshot);
}

void resetScheduler() {
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
  externalInterruptRequests = {};
  externalInterruptFrames.clear();
  executingInstruction = false;
  trapFrames.clear();
  pendingTimerTrap = {};
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

int queueExternalInterrupt(uint32_t level, uint32_t workspace, uint32_t iptr, uint32_t status) {
  if (!portableInstructionAbiEnabled()) return PORTABLE_ABI_DISABLED;
  if (level >= 8 || workspace == 0 || (workspace & 3U) || (status & STATUS_INTERRUPTED_OPERATION_MASK))
    return INVALID_EXTERNAL_INTERRUPT;
  externalInterruptRequests[level] = {true, workspace, iptr, status};
  return 0;
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

int serviceExternalInterrupts() {
  uint32_t mask = cpuState.priority == LOW_PRIORITY ? LP_EXTERNALEVENT_ENB : HP_EXTERNALEVENT_ENB;
  if (!(omrState.Enables & mask)) return 0;
  for (int level = 7; level >= 0; --level) {
    auto &request = externalInterruptRequests[static_cast<std::size_t>(level)];
    if (!request.pending || level <= activeExternalInterruptLevel()) continue;
    int result = enterExternalInterrupt(static_cast<uint32_t>(level), request.workspace, request.iptr, request.status);
    if (result) return result;
    request.pending = false;
    return 0;
  }
  return 0;
}

int pollTimers() {
  if (omrState.halted) return CPU_HALTED;
  int result = releaseTimers(omrState.ClockRegHP, omrState.ClockRegLP, 0, 0);
  if (result || executingInstruction) return result;
  if (peripheralPoller) {
    result = peripheralPoller();
    if (result) return result;
  }
  result = serviceExternalInterrupts();
  if (result) return result;
  result = pollExternalChannels();
  if (result) return result;
  if (!executingInstruction && !trapHandlerActive() && !interruptHandlerActive() && pendingTimerTrap.cause) {
    if (cpuState.running && cpuState.priority == LOW_PRIORITY && cpuState.interruptsDisabled &&
        pendingTimerTrap.priority == HIGH_PRIORITY) return dispatchProcess();
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

bool validPendingOperation(const CPUSTATE &state) {
  uint32_t operation = (state.status & STATUS_INTERRUPTED_OPERATION_MASK) >> 26;
  if (operation == 0) return true;
  if ((operation == 6 || operation == 7) && (state.status & STATUS_PORTABLE_ABI) && state.running &&
      state.move2dBlockLength == 0 && state.areg == 0 &&
      !(state.status & STATUS_BUFFERED_WORD)) return true;
  if (!(state.status & STATUS_PORTABLE_ABI) || operation > 7 || !state.running ||
      state.move2dBlockLength == 0 || state.moveRowWidth == 0 ||
      state.areg == 0 || state.areg > state.moveRowWidth) return false;
  if (state.status & STATUS_BUFFERED_WORD) {
    uint32_t width = ((state.status & STATUS_BUFFER_WIDTH) >> 22) + 1;
    if (width > state.areg || ((operation == 4 || operation == 5) && width != 1)) return false;
  }
  return true;
}

int completePendingChannel() {
  uint32_t descriptor = cpuState.move2dSourceStride;
  if (descriptor == NO_PROCESS) {
    auto found = externalChannels.find(cpuState.move2dDestStride);
    if (found == externalChannels.end()) return CHANNEL_HARDWARE_UNSUPPORTED;
    found->second.active = NO_PROCESS;
    cpuState.status &= ~STATUS_INTERRUPTED_OPERATION_MASK;
    cpuState.areg = cpuState.breg = cpuState.creg = undefinedWord;
    return 0;
  }
  unsigned char priority = static_cast<unsigned char>(descriptor & 1U);
  bool trapped = (trapEnableMask(priority) & INTERNALCH_TRAPENB) != 0;
  if (trapped) {
    uint32_t workspace = 0;
    int result = readProcessWord(trapRecordAddress(priority, TRAP_GROUP_SCHEDULER, false), 2, workspace);
    if (result || workspace == 0 || (workspace & 3U)) return INVALID_TRAP_HANDLER;
  }
  uint32_t previousStatus = cpuState.status;
  cpuState.status &= ~STATUS_INTERRUPTED_OPERATION_MASK;
  int result = trapped ? requestTrap(INTERNALCH_TRAPENB, priority, false, descriptor)
                       : enqueueProcess(descriptor & WordSelectMask, priority);
  if (result) cpuState.status = previousStatus;
  else if (!trapped) cpuState.areg = cpuState.breg = cpuState.creg = undefinedWord;
  return result;
}

int beginResumableMove(uint32_t operation, uint32_t length, uint32_t destination, uint32_t source) {
  if (interruptHandlerActive()) return TRAP_HANDLER_BLOCKED;
  if (operation == 0 || operation > 7) return INVALID_PORTABLE_STATE;
  if (operation == 2) {
    int result = validateDeviceMoveRange(source, destination, length);
    if (result) return result;
  }
  if (operation <= 2 || operation >= 6) cpuState.move2dBlockLength = 1;
  cpuState.moveRowWidth = length;
  cpuState.moveBufferedWord = 0;
  cpuState.status = (cpuState.status & ~(STATUS_INTERRUPTED_OPERATION_MASK | STATUS_BUFFERED_WORD | STATUS_BUFFER_WIDTH)) |
                    STATUS_PORTABLE_ABI | (operation << 26);
  cpuState.areg = length;
  cpuState.breg = destination;
  cpuState.creg = source;
  if (length == 0 || cpuState.move2dBlockLength == 0) {
    cpuState.status &= ~STATUS_INTERRUPTED_OPERATION_MASK;
    cpuState.areg = cpuState.breg = cpuState.creg = undefinedWord;
    return 0;
  }
  return resumePendingOperation();
}

int resumePendingOperation() {
  if (!portableInstructionAbiEnabled()) return PORTABLE_ABI_DISABLED;
  if (!validPendingOperation(cpuState)) return INVALID_PORTABLE_STATE;
  uint32_t owner = cpuState.wptr;
  unsigned char priority = cpuState.priority;
  std::size_t trapDepth = trapFrames.size();
  std::size_t interruptDepth = externalInterruptFrames.size();
  uint32_t operation = (cpuState.status & STATUS_INTERRUPTED_OPERATION_MASK) >> 26;
  while (operation != 0) {
    uint32_t remaining = cpuState.areg;
    if (remaining == 0 && operation >= 6) return completePendingChannel();
    uint32_t width = operation == 4 || operation == 5 ? 1U : std::min<uint32_t>(4, remaining);
    bool external = operation >= 6 && cpuState.move2dSourceStride == NO_PROCESS;
    auto endpoint = external ? externalChannels.find(cpuState.move2dDestStride) : externalChannels.end();
    if (external && endpoint == externalChannels.end()) return CHANNEL_HARDWARE_UNSUPPORTED;
    if (!(cpuState.status & STATUS_BUFFERED_WORD)) {
      uint32_t value = 0;
      int result = 0;
      if (external && operation == 6) {
        if (endpoint->second.input.size() < width) return INVALID_CHANNEL;
        for (uint32_t index = 0; index < width; ++index) {
          value |= static_cast<uint32_t>(endpoint->second.input.front()) << (index * 8U);
          endpoint->second.input.pop_front();
        }
      } else result = readBytes(cpuState.creg, static_cast<int>(width), &value);
      if (result) return result;
      cpuState.moveBufferedWord = value;
      cpuState.status = (cpuState.status & ~STATUS_BUFFER_WIDTH) | STATUS_BUFFERED_WORD | ((width - 1) << 22);
    } else width = ((cpuState.status & STATUS_BUFFER_WIDTH) >> 22) + 1;
    bool write = (operation != 4 && operation != 5) ||
                 (operation == 4 ? cpuState.moveBufferedWord == 0 : cpuState.moveBufferedWord != 0);
    if (write) {
      int result = 0;
      if (external && operation == 7) {
        if (endpoint->second.capacity - endpoint->second.output.size() < width) return INVALID_CHANNEL;
        for (uint32_t index = 0; index < width; ++index)
          endpoint->second.output.push_back(static_cast<uint8_t>(cpuState.moveBufferedWord >> (index * 8U)));
      } else result = storeBytes(cpuState.breg, static_cast<int>(width), cpuState.moveBufferedWord);
      if (result) return result;
    }
    cpuState.areg = remaining - width;
    cpuState.breg = cpuState.breg + width;
    cpuState.creg = cpuState.creg + width;
    cpuState.status &= ~(STATUS_BUFFERED_WORD | STATUS_BUFFER_WIDTH);
    if (cpuState.areg == 0) {
      --cpuState.move2dBlockLength;
      if (cpuState.move2dBlockLength == 0) {
        if (operation >= 6) return completePendingChannel();
        cpuState.status &= ~STATUS_INTERRUPTED_OPERATION_MASK;
        cpuState.areg = cpuState.breg = cpuState.creg = undefinedWord;
        return 0;
      }
      cpuState.breg = cpuState.breg - cpuState.moveRowWidth + cpuState.move2dDestStride;
      cpuState.creg = cpuState.creg - cpuState.moveRowWidth + cpuState.move2dSourceStride;
      cpuState.areg = cpuState.moveRowWidth;
    }
    int result = advanceTimers(write ? 2 : 1);
    if (result) return result;
    bool previousExecution = executingInstruction;
    executingInstruction = false;
    result = pollTimers();
    executingInstruction = previousExecution;
    if (result) return result;
    if (cpuState.wptr != owner || cpuState.priority != priority || trapFrames.size() != trapDepth ||
      externalInterruptFrames.size() != interruptDepth) return 0;
  }
  return 0;
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
  TrapFrame frame{cause, priority, generated, cpuState};
  uint32_t enables = (omrState.Enables & ~0x3fffU) | trapEnableMask(priority);
  uint32_t status = cpuState.status | cause | (generated ? 0x8000U : 0U);
  if (group == TRAP_GROUP_SCHEDULER) status = (status & ~0x30000U) | (static_cast<uint32_t>(cpuState.priority) << 16);
  std::array<uint32_t, 4> saved{enables, status, cpuState.wptr, cpuState.iptr};
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
  if (portableInstructionAbiEnabled()) omrState.Enables &= descriptorWords[0];
  else omrState.Enables = (descriptorWords[0] & 0xffff0000U) |
                         (omrState.Enables & descriptorWords[0] & 0xffffU);
  cpuState.status = descriptorWords[1];
  if (group == TRAP_GROUP_SCHEDULER)
    cpuState.status = (cpuState.status & ~0x30000U) | (static_cast<uint32_t>(frame.caller.priority) << 16);
  cpuState.wptr = descriptorWords[2];
  cpuState.iptr = descriptorWords[3];
  cpuState.nWptr = 0;
  cpuState.running = true;
  /* Areg, Breg and Creg are not saved and keep the trapped process's values (ST20-TP2
   * datasheet 3.6.3); the descriptor is passed in the handler's entry workspace word 0. */
  return 0;
}

void setPortableInstructionAbi(bool enabled) {
  portableInstructionAbi = enabled;
}

bool portableInstructionAbiEnabled() {
  return portableInstructionAbi;
}

void setInterruptReturnObserver(InterruptReturnObserver observer) {
  interruptReturnObserver = observer;
}

void setPeripheralPoller(PeripheralPoller poller) {
  peripheralPoller = poller;
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
    if ((saved[0] & STATUS_INTERRUPTED_OPERATION_MASK) && (!managed || !portableInstructionAbiEnabled()))
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
    if (!validPendingOperation(restored)) return INVALID_PORTABLE_STATE;
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
    if ((lowPriorityShadow.status & STATUS_INTERRUPTED_OPERATION_MASK) && !portableInstructionAbiEnabled())
      return INTERRUPTED_OPERATION_UNSUPPORTED;
    if (!validPendingOperation(lowPriorityShadow)) return INVALID_PORTABLE_STATE;
    selectPriority(LOW_PRIORITY);
    cpuState = lowPriorityShadow;
    lowPriorityShadowValid = false;
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
  bool nativeCrossPriority = !portableInstructionAbiEnabled() && priority != frame.priority;
  for (int index = 0; index < (nativeCrossPriority ? 2 : 4); ++index) {
    int result = readProcessWord(trapped, index, saved[static_cast<std::size_t>(index)]);
    if (result) return result;
  }
  if (!nativeCrossPriority && (saved[2] & 3U)) return INVALID_TRAP_RETURN;
  uint32_t enablesMask = portableInstructionAbiEnabled() ? 0x00ff3fffU : 0xffff3fffU;
  omrState.Enables = (omrState.Enables & ~enablesMask) | (saved[0] & enablesMask);
  omrState.TrapEnables[frame.priority] = saved[0] & 0x3fffU;
  selectPriority(priority);
  if (priority != frame.priority) cpuState = frame.caller;
  cpuState.priority = priority;
  if (!nativeCrossPriority) cpuState.status = saved[1] & ~frame.cause;
  if (!portableInstructionAbiEnabled() && !nativeCrossPriority && group == TRAP_GROUP_SCHEDULER)
    cpuState.status = (cpuState.status & ~0x30000U) | (frame.caller.status & 0x30000U);
  cpuState.interruptsDisabled = frame.caller.interruptsDisabled;
  cpuState.timeslicePeriods = nativeCrossPriority || saved[2] == frame.caller.wptr ? frame.caller.timeslicePeriods : 0;
  cpuState.moveRowWidth = frame.caller.moveRowWidth;
  cpuState.moveBufferedWord = frame.caller.moveBufferedWord;
  cpuState.move2dBlockLength = frame.caller.move2dBlockLength;
  cpuState.move2dDestStride = frame.caller.move2dDestStride;
  cpuState.move2dSourceStride = frame.caller.move2dSourceStride;
  if (frame.generated)
    cpuState.status = (cpuState.status & ~0x8000U) | (frame.caller.status & 0x8000U);
  if (!nativeCrossPriority) {
    cpuState.wptr = saved[2];
    cpuState.iptr = saved[3];
  }
  cpuState.nWptr = frame.caller.nWptr;
  cpuState.running = nativeCrossPriority ? frame.caller.running
      : (saved[1] & STATUS_VALID) != 0 || frame.caller.running || saved[2] != frame.caller.wptr ||
        saved[3] != static_cast<uint32_t>(frame.caller.iptr);
  if (cpuState.running) queueEmptyReported[cpuState.priority] = false;
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
  std::unordered_set<uint32_t> visited;
  while (cursor != NO_PROCESS) {
    if ((cursor & 3U) || !visited.insert(cursor).second) return INVALID_PROCESS_LIST;
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
  if (portableInstructionAbiEnabled() && length != 0) {
    result = writeProcessWord(channel, 0, NO_PROCESS);
    if (result) return result;
    cpuState.move2dDestStride = channel;
    cpuState.move2dSourceStride = descriptor;
    return beginResumableMove(input ? 6U : 7U, length, destination, source);
  }
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
  if (!softwareChannelAddress(channel)) return CHANNEL_HARDWARE_UNSUPPORTED;
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
  bool portable = portableInstructionAbiEnabled();
  if (selection == 0) return 0;
  if (load && priority == cpuState.priority && (selection & 7U)) return INVALID_SHADOW_ACCESS;
  if ((selection & 6U) && priority != cpuState.priority && !portable && priority != LOW_PRIORITY)
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
  if ((selection & 8U) && !portable) {
    if (priority != cpuState.priority && priority != LOW_PRIORITY) return INVALID_SHADOW_ACCESS;
    if (bank->status & STATUS_INTERRUPTED_OPERATION_MASK) return INTERRUPTED_OPERATION_UNSUPPORTED;
  }
  /* The C2 core transfers four block-move words (7-10), not the five (7-11) the instruction
   * set manual lists: OS20 (os20i.h NO_MOVE2D_WORDS, "Instruction set manual wrong") packs its
   * per-priority queues so that word 11 belongs to the next queue. Word 11 is portable-only. */
  const std::size_t recordWords = portable ? 12 : 11;
  auto selectedWord = [selection, recordWords](std::size_t index) {
    return index < recordWords &&
           (selection & (index == 0 ? 1U : index <= 3 ? 2U : index <= 6 ? 4U : 8U)) != 0;
  };
  const uint32_t enablesMask = portable ? 0x00ff3fffU : 0xffff3fffU;
  const uint32_t globalMask = enablesMask & 0xffff0000U;
  std::array<uint32_t, 12> words{};
  if (load) {
    for (std::size_t index = 0; index < words.size(); ++index) {
      if (!selectedWord(index)) continue;
      int result = readProcessWord(buffer, static_cast<int32_t>(index), words[index]);
      if (result) return result;
    }
    if (selection & 2U) {
      if ((words[1] & STATUS_INTERRUPTED_OPERATION_MASK) && !portable) return INTERRUPTED_OPERATION_UNSUPPORTED;
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
      if (portable && (words[1] & STATUS_PORTABLE_ABI))
        updated.interruptsDisabled = (words[1] & STATUS_PORTABLE_INTDIS) != 0;
      updated.wptr = words[2];
      updated.iptr = words[3];
      updated.nWptr = 0;
      updated.timeslicePeriods = 0;
      valid = (words[1] & STATUS_VALID) != 0;
      updated.running = valid;
      if (!portable && !valid) updated.interruptsDisabled = false;
    }
    if (selection & 4U) {
      updated.areg = words[4];
      updated.breg = words[5];
      updated.creg = words[6];
    }
    if ((selection & 8U) && portable) {
      updated.moveRowWidth = words[7];
      updated.move2dBlockLength = words[8];
      updated.move2dDestStride = words[9];
      updated.move2dSourceStride = words[10];
      updated.moveBufferedWord = words[11];
    }
    if (!validPendingOperation(updated)) return INVALID_PORTABLE_STATE;
    if (portable && savedFrame && (selection & 3U)) {
      uint32_t address = trapRecordAddress(savedFrame->priority, trapGroup(savedFrame->cause), true);
      if (selection & 1U) {
        uint32_t enables = 0;
        int result = readProcessWord(address, 0, enables);
        if (!result) result = writeProcessWord(address, 0, (enables & ~enablesMask) | (words[0] & enablesMask));
        if (result) return result;
      }
      if (selection & 2U) {
        uint32_t status = updated.status | savedFrame->cause;
        if (trapGroup(savedFrame->cause) == TRAP_GROUP_SCHEDULER)
          status = (status & ~0x30000U) | (priority << 16);
        int result = writeProcessWord(address, 1, status);
        if (!result) result = writeProcessWord(address, 2, updated.wptr);
        if (!result) result = writeProcessWord(address, 3, updated.iptr);
        if (result) return result;
      }
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
  if (!valid && (portable || !savedFrame)) {
    source = {undefinedWord, undefinedWord,
              undefinedWord, undefinedWord, 0,
              static_cast<unsigned char>(priority), NO_PROCESS, false, 0, 0, 0,
              portable ? 0U : source.status & ~STATUS_VALID};
  }
  if (selection & 1U) {
    if (portable) {
      int result = readProcessWord(buffer, 0, words[0]);
      if (result && (result != READ_UNUSED_MEM || isMemoryDeviceAddress(buffer))) return result;
    }
    words[0] = (words[0] & ~enablesMask) | (omrState.Enables & globalMask) |
               trapEnableMask(static_cast<unsigned char>(priority));
  }
  words[1] = valid ? source.status | STATUS_VALID : source.status & ~STATUS_VALID;
  if (portable) words[1] = (words[1] & ~STATUS_PORTABLE_INTDIS) | STATUS_PORTABLE_ABI |
                          (source.interruptsDisabled ? STATUS_PORTABLE_INTDIS : 0U);
  words[2] = source.wptr;
  words[3] = source.iptr;
  words[4] = source.areg;
  words[5] = source.breg;
  words[6] = source.creg;
  words[7] = source.moveRowWidth;
  words[8] = source.move2dBlockLength;
  words[9] = source.move2dDestStride;
  words[10] = source.move2dSourceStride;
  words[11] = source.moveBufferedWord;
  if (!portable) {
    for (std::size_t index = 7; index < words.size(); ++index) words[index] = undefinedWord;
  }
  for (std::size_t index = 0; index < words.size(); ++index) {
    if (!selectedWord(index)) continue;
    int result = writeProcessWord(buffer, static_cast<int32_t>(index), words[index]);
    if (result) return result;
  }
  return 0;
}

int restartProcess(uint32_t record) {
  if (interruptHandlerActive()) return TRAP_HANDLER_BLOCKED;
  bool portable = portableInstructionAbiEnabled();
  std::array<uint32_t, 12> words{};
  /* a native restart uses only the enables, control and evaluation-stack words */
  int recordWords = portable ? 12 : 7;
  for (int index = 0; index < recordWords; ++index) {
    int result = readProcessWord(record, index, words[static_cast<std::size_t>(index)]);
    if (result) return result;
  }
  if (!portable && (words[1] & STATUS_INTERRUPTED_OPERATION_MASK)) return INTERRUPTED_OPERATION_UNSUPPORTED;
  if (portable && !(words[1] & STATUS_PORTABLE_ABI)) return INVALID_PORTABLE_STATE;
  if ((words[1] & STATUS_VALID) && (words[2] & 3U))
    return portable ? INVALID_PORTABLE_STATE : INVALID_PROCESS_LIST;
  CPUSTATE updated{words[4], words[5], words[6],
      words[3], 0, cpuState.priority, words[2], (words[1] & STATUS_VALID) != 0,
      portable ? words[8] : 0U, portable ? words[9] : 0U, portable ? words[10] : 0U,
      words[1], portable && (words[1] & STATUS_PORTABLE_INTDIS) != 0,
      0, portable ? words[7] : 0U, portable ? words[11] : 0U};
  if (!validPendingOperation(updated)) return INVALID_PORTABLE_STATE;
  uint32_t enablesMask = portable ? 0x00ff3fffU : 0xffff3fffU;
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
  std::unordered_set<uint32_t> visited;
  while (next != NO_PROCESS) {
    if ((next & 3U) || next == cpuState.wptr || !visited.insert(next).second)
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

int st20Init(PARMS *userParms) {
  int i;
  uint32_t value;

  for (i = 0; i < userParms->nParms; i++) {
    if (!strcmp(userParms->parameter[i], "PORTABLE_INSTRUCTION_ABI")) {
      if (sscanf(userParms->value[i], "%" SCNx32, &value) == 1 && (value == 0 || value == 1))
        setPortableInstructionAbi(value != 0);
    } else if (!strcmp(userParms->parameter[i], ST20_PRODUCT_ID_CH)) {
      if (sscanf(userParms->value[i], "%" SCNx32, &value) == 1) {
        st20ProductId = value;
      }
    } else if (!strcmp(userParms->parameter[i], MEM_START_VAL_CH)) {
      if (sscanf(userParms->value[i], "%" SCNx32, &value) == 1) {
        memStartVal = value;
      }
    } else if (!strcmp(userParms->parameter[i], TIMER_GUESS_CH)) {
      if (sscanf(userParms->value[i], "%" SCNx32, &value) == 1) {
        timerGuess = value;
      }
    } else if (!strcmp(userParms->parameter[i], WPTR_END_ADDR_CH)) {
      if (sscanf(userParms->value[i], "%" SCNx32, &value) == 1) {
        wptrEndAddr = value;
      }
    } else if (!strcmp(userParms->parameter[i], START_ADDR_CH)) {
      if (sscanf(userParms->value[i], "%" SCNx32, &value) == 1) {
        startAddr = value;
      }
    }
  }

  compat::print("START_ADDR=0x{:08x}\nMEM_START_VAL=0x{:08x}\nST20_PRODUCT_ID=0x{:08x}\nTIMER_GUESS=0x{:08x}\nWPTR_END_ADDR=0x{:08x}\n",
                startAddr, memStartVal, st20ProductId, timerGuess, wptrEndAddr);

  initCPUState();
  initWatch();
  // added for timers
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

  char cpuFileName[NAME_SIZE];
  if (snprintf(cpuFileName, sizeof(cpuFileName), "%s/cpu.bin", dirName) < 0) {
    return INVALID_CPU_FILENAME;
  }

  constexpr mode_t fileMode = S_IRUSR | S_IWUSR;
  int cpuFileFd = open(cpuFileName, O_WRONLY | O_CREAT | O_TRUNC, fileMode);
  if (cpuFileFd < 0) {
    perror("CPU state save file cannot be opened");
    return INVALID_CPU_FILE;
  }

  // helper lambda to ensure descriptor is closed on failure
  auto cleanWrite = [cpuFileFd](const void *buf, size_t count) -> bool {
    return write(cpuFileFd, buf, count) == static_cast<ssize_t>(count);
  };

  CPUSnapshotPrefix snapshot{cpuState.areg, cpuState.breg, cpuState.creg,
                             cpuState.iptr, 0, cpuState.nWptr, {}};
  if (!cleanWrite(&snapshot, sizeof(snapshot))) {
    close(cpuFileFd);
    return INVALID_CPU_WRITE;
  }

  wptrEndAddr = static_cast<uint32_t>(cpuState.wptr + static_cast<uint32_t>(cpuState.nWptr) * BytesPerWord - 1U);
  if (!cleanWrite(&wptrEndAddr, sizeof(wptrEndAddr))) {
    close(cpuFileFd);
    return INVALID_CPU_WRITE;
  }

  if (!cleanWrite(&watch, sizeof(WATCH))) {
    close(cpuFileFd);
    return INVALID_CPU_WRITE;
  }

  RuntimeSnapshot runtime = captureRuntime();
  if (!cleanWrite(runtime.data(), sizeof(runtime))) {
    close(cpuFileFd);
    return INVALID_CPU_WRITE;
  }
  if (!externalChannels.empty()) {
    uint64_t totalBytes = 0;
    for (const auto &[address, endpoint] : externalChannels) {
      (void)address;
      totalBytes += endpoint.input.size() + endpoint.output.size();
    }
    if (totalBytes > 16777216) {
      close(cpuFileFd);
      return INVALID_CPU_WRITE;
    }
    std::array<uint32_t, 4> header{0x45584348U, 1, static_cast<uint32_t>(externalChannels.size()), static_cast<uint32_t>(totalBytes)};
    if (!cleanWrite(header.data(), sizeof(header))) {
      close(cpuFileFd);
      return INVALID_CPU_WRITE;
    }
    for (const auto &[address, endpoint] : externalChannels) {
      std::array<uint32_t, 12> record{address, endpoint.capacity, endpoint.waiter, endpoint.active,
          endpoint.length, endpoint.inputWait, endpoint.queued, endpoint.altOwner, endpoint.altWake,
          endpoint.altNotified, static_cast<uint32_t>(endpoint.input.size()), static_cast<uint32_t>(endpoint.output.size())};
      std::vector<uint8_t> bytes(endpoint.input.begin(), endpoint.input.end());
      bytes.insert(bytes.end(), endpoint.output.begin(), endpoint.output.end());
      if (!cleanWrite(record.data(), sizeof(record)) || !cleanWrite(bytes.data(), bytes.size())) {
        close(cpuFileFd);
        return INVALID_CPU_WRITE;
      }
    }
  }

  close(cpuFileFd);
  return 0;
}

uint32_t get_iptr(void) {
  return (cpuState.iptr);
}

int loadCPUState(const char *dirName) {
  if (dirName == nullptr || *dirName == '\0') {
    return INVALID_CPU_FILENAME;
  }

  char cpuFileName[NAME_SIZE];
  if (snprintf(cpuFileName, sizeof(cpuFileName), "%s/cpu.bin", dirName) < 0) {
    return INVALID_CPU_FILENAME;
  }

  int cpuFileFd = open(cpuFileName, O_RDONLY);
  if (cpuFileFd < 0) {
    perror("CPU state load file cannot be opened");
    return INVALID_CPU_FILE;
  }

  /* helper lambda to check exact byte read counts and prevent FD leaks on failure */
  auto cleanRead = [cpuFileFd](void *buf, size_t count) -> bool {
    return read(cpuFileFd, buf, count) == static_cast<ssize_t>(count);
  };

  CPUSnapshotPrefix snapshot{};
  if (!cleanRead(&snapshot, sizeof(snapshot))) {
    close(cpuFileFd);
    return INVALID_CPU_READ;
  }

  uint32_t loadedEndAddress = 0;
  if (!cleanRead(&loadedEndAddress, sizeof(loadedEndAddress))) {
    close(cpuFileFd);
    return INVALID_CPU_READ;
  }

  WATCH loadedWatch{};
  if (!cleanRead(&loadedWatch, sizeof(loadedWatch))) {
    close(cpuFileFd);
    return INVALID_CPU_READ;
  }

  RuntimeSnapshot runtime{};
  if (snapshot.nWptr < 0 || snapshot.nWptr > MAX_WPTR ||
      !cleanRead(runtime.data(), sizeof(runtime)) || !validRuntime(runtime)) {
    close(cpuFileFd);
    return INVALID_CPU_FILE;
  }
  {
    CPUSTATE candidate{snapshot.areg, snapshot.breg, snapshot.creg, snapshot.iptr};
    candidate.status = runtime[RuntimeStatus];
    candidate.running = runtime[RuntimeRunning] != 0;
    candidate.moveRowWidth = runtime[RuntimeMoveData];
    candidate.moveBufferedWord = runtime[RuntimeMoveData + 1];
    candidate.move2dBlockLength = runtime[RuntimeMove2dRows];
    if (!validPendingOperation(candidate)) {
      close(cpuFileFd);
      return INVALID_CPU_FILE;
    }
  }
  std::unordered_map<uint32_t, ExternalChannel> loadedChannels;
  {
    std::array<uint32_t, 4> header{};
    ssize_t headerBytes = read(cpuFileFd, header.data(), sizeof(header));
    if (headerBytes != 0) {
      if (headerBytes != static_cast<ssize_t>(sizeof(header)) || header[0] != 0x45584348U || header[1] != 1 ||
          header[2] > 1024 || header[3] > 16777216) {
        close(cpuFileFd);
        return INVALID_CPU_FILE;
      }
      uint64_t total = 0;
      auto validDescriptor = [](uint32_t value) {
        return value == NO_PROCESS || (!(value & 2U) && (value & WordSelectMask) != NO_PROCESS);
      };
      for (uint32_t index = 0; index < header[2]; ++index) {
        std::array<uint32_t, 12> record{};
        if (!cleanRead(record.data(), sizeof(record))) {
          close(cpuFileFd);
          return INVALID_CPU_FILE;
        }
        uint64_t bytes = static_cast<uint64_t>(record[10]) + record[11];
        total += bytes;
        if ((record[0] & 3U) || loadedChannels.contains(record[0]) || record[1] == 0 || record[1] > 1048576 ||
            record[4] > record[1] || record[5] > 1 || record[6] > 1 || record[8] > 1 || record[9] > 1 ||
            record[10] > record[1] || record[11] > record[1] || total > header[3] ||
            !validDescriptor(record[2]) || !validDescriptor(record[3]) || !validDescriptor(record[7])) {
          close(cpuFileFd);
          return INVALID_CPU_FILE;
        }
        std::vector<uint8_t> data(static_cast<std::size_t>(bytes));
        if (!cleanRead(data.data(), data.size())) {
          close(cpuFileFd);
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
        close(cpuFileFd);
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
  close(cpuFileFd);
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
  // compat::println("-GLOBAL INTERRUPTS ENABLES VALUES-\n");
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
  // compat::println("-TRAP ENABLES VALUES-");
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
int decodeNextInstr() {
  bool foundInstr = false;
  uint32_t cByte = 0;
  int result = 0;
  result = prepareProcess();
  if (result) return result;

  if (cpuState.status & STATUS_INTERRUPTED_OPERATION_MASK) {
    if (!validPendingOperation(cpuState)) return INVALID_PORTABLE_STATE;
    uint32_t operation = (cpuState.status & STATUS_INTERRUPTED_OPERATION_MASK) >> 26;
    const uint32_t entries[] = {0, 0x5a, 0x1d4, 0x6c, 0x6e, 0x6d, 0x17, 0x1b};
    instrCode = entries[operation];
    operand = 0;
    instrLength = 0;
    return 0;
  }

  instrCode = 0;
  operand = 0;
  instrLength = 0;

  /* keep reading a byte until we've found a non-prefix byte */
  while (!foundInstr) {

    /* read the next byte of the current instruction */
    result = readBytes(cpuState.iptr + static_cast<uint32_t>(instrLength), 1, &cByte);
    if (result) {
      compat::println("{}", memoryError(result));
      compat::println("Error occurred when reading instruction at {:08x}, offset {:02x}",
                      cpuState.iptr, instrLength);
      return (-1);
    }

    /* save the bytes that comprise the current instruction */
    instrBytes[instrLength++] = static_cast<unsigned char>(cByte & 0xFF);

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
      foundInstr = false;
      break;

    case 0x06:
      operand = ~operand;
      foundInstr = false;
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

      foundInstr = true;

      break;

    /* all of the non-Fx single byte instructions are handled here */
    default:
      foundInstr = true;
      break;
    }
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

//////////////////////////////////////////////////////////////////////////
//					EXEC DECODED INSTRUCTION
//				(Here the we tick the timers too)
//////////////////////////////////////////////////////////////////////////
uint64_t elapsedCpuCycles() {
  return cpuCycles;
}

int advanceTimers(uint64_t cycles) {
  cpuCycles += cycles;
  auto advanceClock = [cycles](uint32_t &clock, uint64_t &phase, uint32_t frequency) {
    uint64_t elapsed = phase + (cycles % CPU_CLOCK) * frequency;
    uint64_t ticks = (cycles / CPU_CLOCK) * frequency + elapsed / CPU_CLOCK;
    phase = elapsed % CPU_CLOCK;
    clock += static_cast<uint32_t>(ticks);
    return ticks;
  };
  uint32_t oldHP = omrState.ClockRegHP;
  uint32_t oldLP = omrState.ClockRegLP;
  uint64_t ticksHP = 0;
  uint64_t ticksLP = 0;
  if (omrState.ClockEnables & HPTIMER_MASK)
    ticksHP = advanceClock(omrState.ClockRegHP, omrState.ClockPhaseHP, HP_TIMER_HZ);
  if (omrState.ClockEnables & LPTIMER_MASK)
    ticksLP = advanceClock(omrState.ClockRegLP, omrState.ClockPhaseLP, LP_TIMER_HZ);
  if (omrState.halted) return CPU_HALTED;
  uint32_t unusedClock = 0;
  uint64_t sliceTicks = advanceClock(unusedClock, omrState.TimeslicePhase, 1000);
  auto ageQuantum = [sliceTicks](CPUSTATE &state) {
    if (state.running && state.priority == LOW_PRIORITY)
      state.timeslicePeriods = static_cast<uint32_t>(std::min<uint64_t>(2, state.timeslicePeriods + sliceTicks));
  };
  ageQuantum(cpuState);
  if (lowPriorityShadowValid) ageQuantum(lowPriorityShadow);
  for (TrapFrame &frame : trapFrames) ageQuantum(frame.caller);
  int result = releaseTimers(oldHP, oldLP, ticksHP, ticksLP);
  return result || executingInstruction ? result : pollTimers();
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
  if (cpuState.status & STATUS_INTERRUPTED_OPERATION_MASK) {
    executingInstruction = true;
    result = resumePendingOperation();
    executingInstruction = false;
    if (!result) result = pollTimers();
    *breakFlag = checkWatch();
    return result;
  }
  cpuState.iptr = instructionAddress + static_cast<uint32_t>(instrLength);

  executingInstruction = true;
  result = advanceTimers(instrEntry[instrCode].cpucycles);
  if (!result) result = instrEntry[instrCode].function(operand);
  else cpuState.iptr = instructionAddress;
  executingInstruction = false;
  if (!result) result = pollTimers();

  *breakFlag = checkWatch();

  return (result);
}

/**************************
 */
int readWptrWord(uint32_t index, uint32_t *value) {
  int result = 0;
  uint32_t address;

  addrWptrWord(index, &address);

  result = readBytes(address, BytesPerWord, value);

  return result;
}

/**************************
 */
int addrWptrWord(uint32_t index, uint32_t *address) {
  *address = cpuState.wptr + index * BytesPerWord;

  return 0;
}

/**************************
 */
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
  case INVALID_PORTABLE_STATE:
    return "Invalid portable emulator state or floating-point input";
  case PORTABLE_ABI_DISABLED:
    return "Emulator-specific instruction ABI is disabled";
  case INVALID_EXTERNAL_INTERRUPT:
    return "Invalid portable external interrupt request";
  default:
    return "Unknown st20 error";
  }
}

void resetTimers() {
  omrState.ClockRegHP = timerGuess;
  omrState.ClockRegLP = timerGuess;
  omrState.ClockPhaseHP = 0;
  omrState.ClockPhaseLP = 0;
  omrState.ClockEnables |= HPTIMER_MASK; // set bit0
  omrState.ClockEnables |= LPTIMER_MASK; // set bit1
  omrState.Enables = 0xffffc000;         // the initial state, see omr.h
                                         // bit 14 and 15 are reserved but set to 1
  //'ticking' code is into execInstr() subroutine
}

int initTimer() {
  resetTimers();
  compat::println("CPU_CLOCK={:d} Hz\n HPTimer={:d} Hz\n LPTimer={:d} Hz",
                  CPU_CLOCK, HP_TIMER_HZ, LP_TIMER_HZ);

  return (0);
}
