#if defined(_WIN32)
#include <simdll.h>

_Static_assert(sizeof(INT32) == 4, "Simulator callbacks require 32-bit words");
_Static_assert(sizeof(void *) == 4, "Simulator DLL requires a 32-bit target");

static unsigned char storage[4096];
static Dll_simmem_fn *memory_access;
static Dll_simstate_fn *state_access;
static unsigned int reads;
static int triggered;
static int pending_tick;
static int signal_mode;
static int tick_mode;

Dll_export Dll_status_t continuation_init(void **handle, const char *const parameters,
    Ilevel_desc_t level, Dll_simmem_fn *memory, Signal_arbitrate_s *signals,
    Dll_simstate_fn *state) {
  signal_mode = parameters && parameters[0] == 's';
  tick_mode = parameters && parameters[0] == 't';
  (void)level;
  (void)signals;
  *handle = storage;
  memory_access = memory;
  state_access = state;
  reads = 0;
  triggered = 0;
  pending_tick = 0;
  return memory && state ? DLL_OK : DLL_ERROR;
}

Dll_export Dll_res_t continuation_memory(void *handle, Cycle_cost_s *cycles,
    INT32 address, void *data, size_t length, Mem_access_t access) {
  (void)handle;
  unsigned long offset = (unsigned long)address - 0x40010000UL;
  if (length > sizeof(storage) || offset > sizeof(storage) - length) return DLL_ERROR;
  unsigned char *bytes = data;
  for (size_t index = 0; index < length; ++index) {
    if (ACCESS_WRITE(access)) storage[offset + index] = bytes[index];
    else bytes[index] = storage[offset + index];
  }
  if (cycles) cycles->cycles = 1;
  INT32 priority = 0;
  if (!ACCESS_WRITE(access) && (access == MEM_READ || access == MEM_DEVREAD) &&
      state_access(STATE_C2_PRIORITY, &priority, STATE_REG_READ) == DLL_OK && priority == 1) {
    ++reads;
    if (reads == 2 && !triggered) {
      const State_reg_t registers[] = {STATE_STATUS, STATE_AREG, STATE_BREG, STATE_CREG, STATE_IPTR, STATE_WDESC};
      INT32 count = (INT32)reads;
      memory_access((INT32)0x80006500UL, &count, sizeof(count), MEM_DLLWRITE);
      for (unsigned int index = 0; index < sizeof(registers) / sizeof(registers[0]); ++index) {
        INT32 value = 0;
        if (state_access(registers[index], &value, STATE_REG_READ) != DLL_OK) return DLL_ERROR;
        if (memory_access((INT32)(0x80006504UL + index * 4UL), &value, sizeof(value), MEM_DLLWRITE) != DLL_OK)
          return DLL_ERROR;
      }
      triggered = 1;
      if (tick_mode) return DLL_RES(DLL_OK, EVENT_TICK_HIGH);
        if (signal_mode) pending_tick = 1;
        else {
          INT32 empty = (INT32)0x80000000UL;
          INT32 workspace = (INT32)0x80000100UL;
          if (state_access(STATE_C2_TIMER0, &empty, STATE_REG_WRITE) != DLL_OK ||
          state_access(STATE_FPTR0, &workspace, STATE_REG_WRITE) != DLL_OK ||
          state_access(STATE_BPTR0, &workspace, STATE_REG_WRITE) != DLL_OK) return DLL_ERROR;
        }
    }
  }
  return EVENT_OK;
}

Dll_export Dll_res_t continuation_signal(void *handle, Cycle_cost_s *cycles,
    Dll_signal_t signal, Signal_data_u *data) {
  (void)handle;
  (void)cycles;
  (void)data;
  if (!pending_tick) return EVENT_OK;
  pending_tick = 0;
  INT32 values[3] = {(INT32)signal, (INT32)reads, 0};
  if (state_access(STATE_STATUS, &values[2], STATE_REG_READ) != DLL_OK) return DLL_ERROR;
  for (unsigned int index = 0; index < 3; ++index)
    if (memory_access((INT32)(0x8000651cUL + index * 4UL), &values[index], sizeof(values[index]), MEM_DLLWRITE) != DLL_OK)
      return DLL_ERROR;
  INT32 empty = (INT32)0x80000000UL;
  INT32 workspace = (INT32)0x80000100UL;
  if (state_access(STATE_C2_TIMER0, &empty, STATE_REG_WRITE) != DLL_OK ||
      state_access(STATE_FPTR0, &workspace, STATE_REG_WRITE) != DLL_OK ||
      state_access(STATE_BPTR0, &workspace, STATE_REG_WRITE) != DLL_OK) return DLL_ERROR;
  return EVENT_OK;
}
#endif