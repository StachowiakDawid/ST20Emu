#include "../src/core/cpu/instructions.h"
#include "../src/core/cpu/instrentry.h"
#include "../src/core/cpu/omr.h"
#include "../src/core/cpu/st20.h"
#include "../src/common/compat.h"
#include <cinttypes>
#include "../src/core/memory/memory.h"
#include "../src/soc/sti5518/STi5518_Devices.h"
#include "../src/soc/sti5512/STi5512_Devices.h"

#include <cstdint>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

extern CPUSTATE cpuState;
extern OMRSTATE omrState;
extern INSTRENTRY instrEntry[];
extern uint32_t instrCode;
extern uint32_t operand;
extern uint32_t wptrEndAddr;
extern uint32_t st20ProductId;
extern WATCH watch;

static_assert(std::is_same_v<decltype(CPUSTATE{}.areg), uint32_t>);
static_assert(std::is_same_v<decltype(CPUSTATE{}.breg), uint32_t>);
static_assert(std::is_same_v<decltype(CPUSTATE{}.creg), uint32_t>);
static_assert(std::is_same_v<decltype(CPUSTATE{}.iptr), uint32_t>);
static_assert(std::is_same_v<decltype(WATCH{}.areg), uint32_t>);
static_assert(std::is_same_v<decltype(OMRSTATE{}.Enables), uint32_t>);
static_assert(std::is_same_v<decltype(&readBytes), int (*)(uint32_t, int, uint32_t *)>);
static_assert(std::is_same_v<decltype(&storeBytes), int (*)(uint32_t, int, uint32_t)>);

static int checks = 0;
static int failures = 0;

static void reset(uint32_t areg, uint32_t breg, uint32_t creg) {
  cpuState.areg = areg;
  cpuState.breg = breg;
  cpuState.creg = creg;
  omrState = {};
}

static void expect(const char *name, uint64_t actual, uint64_t expected) {
  ++checks;
  if (actual == expected) return;
  ++failures;
  if (failures <= 20) {
    std::fprintf(stderr, "FAIL %s: actual=0x%" PRIx64 " expected=0x%" PRIx64 "\n", name,
                 static_cast<uint64_t>(actual),
                 static_cast<uint64_t>(expected));
  }
}

static uint32_t nextWord(uint32_t &seed) {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}

static void testWordStack() {
  reset(0x89abcdef, 0x12345678, 0xfedcba98);
  expect("pop returns raw high-bit word", pop(), 0x89abcdef);
  expect("pop rotates B into A", cpuState.areg, 0x12345678);
  expect("pop rotates C into B", cpuState.breg, 0xfedcba98);

  reset(0, 0, 0);
  push(uint64_t{0x1ffffffff});
  expect("push truncates to word width", cpuState.areg, 0xffffffff);
  push(0x80000000U);
  expect("push preserves high-bit word", cpuState.areg, 0x80000000);
  expect("push rotates prior A into B", cpuState.breg, 0xffffffff);
  expect("pop returns unsigned high-bit word", pop(), 0x80000000);
}

static void testCarryArithmetic() {
  struct AddCase {
    uint32_t areg;
    uint32_t breg;
    uint32_t result;
    bool overflow;
  };
  const AddCase additions[] = {
      {0, 0, 0, false},
      {1, 2, 3, false},
      {0xffffffff, 1, 0, false},
      {0xfffffff9, 3, 0xfffffffc, false},
      {0x7ffffffe, 1, 0x7fffffff, false},
      {0x80000001, 0xffffffff, 0x80000000, false},
      {0x7fffffff, 1, 0x80000000, true},
      {0x80000000, 0xffffffff, 0x7fffffff, true},
  };
  for (unsigned char priority : {static_cast<unsigned char>(HIGH_PRIORITY), static_cast<unsigned char>(LOW_PRIORITY)}) {
    for (bool haltOnError : {false, true}) {
      for (const auto &entry : additions) {
        reset(entry.areg, entry.breg, 0x12345678);
        cpuState.priority = priority;
        omrState.HaltOnErrorFlag = haltOnError;
        int expectedReturn = entry.overflow && haltOnError ? CPU_HALTED : 0;
        expect("add return matches overflow mode", add_(0) == expectedReturn, 1);
        expect("add signed result wraps", cpuState.areg, entry.result);
        expect("add moves old C into B", cpuState.breg, 0x12345678);
        expect("add leaves C undefined", cpuState.creg, undefinedWord);
        expect("add sets current priority error", priority == HIGH_PRIORITY ? omrState.HP_ErrorFlag : omrState.LP_ErrorFlag, entry.overflow);
        expect("add preserves other priority error", priority == HIGH_PRIORITY ? omrState.LP_ErrorFlag : omrState.HP_ErrorFlag, 0);
        expect("add halts only on enabled overflow", omrState.halted, entry.overflow && haltOnError);
      }
    }
  }

  uint32_t result = 0;
  expect("N_Add no carry", N_Add(0xfffffffe, 1, &result), 0);
  expect("N_Add boundary sum", result, 0xffffffff);
  expect("N_Add carry", N_Add(0xffffffff, 1, &result), 1);
  expect("N_Add wrapped sum", result, 0);

  uint32_t seed = 0x12345678;
  for (int iteration = 0; iteration < 512; ++iteration) {
    uint32_t areg = nextWord(seed);
    uint32_t breg = nextWord(seed);
    uint32_t creg = nextWord(seed);
    uint64_t sum = static_cast<uint64_t>(areg) + breg + (creg & 1);
    reset(areg, breg, creg);
    lsum_(0);
    expect("lsum low", cpuState.areg, static_cast<uint32_t>(sum));
    expect("lsum carry", cpuState.breg, sum >> 32);

    uint64_t subtrahend = static_cast<uint64_t>(areg) + (creg & 1);
    reset(areg, breg, creg);
    ldiff_(0);
    expect("ldiff low", cpuState.areg,
           static_cast<uint32_t>(static_cast<uint64_t>(breg) - subtrahend));
    expect("ldiff borrow", cpuState.breg, breg < subtrahend);

    uint64_t product = static_cast<uint64_t>(areg) * breg + creg;
    reset(areg, breg, creg);
    lmul_(0);
    expect("lmul low", cpuState.areg, static_cast<uint32_t>(product));
    expect("lmul high", cpuState.breg, product >> 32);
  }

  reset(0xfffffffe, 0, 1);
  lsum_(0);
  expect("lsum boundary carry", cpuState.breg, 0);
  expect("lsum boundary value", cpuState.areg, 0xffffffff);
  reset(0xfffffffe, 0xffffffff, 1);
  ldiff_(0);
  expect("ldiff boundary borrow", cpuState.breg, 0);
  expect("ldiff boundary value", cpuState.areg, 0);
  reset(0xf6daad69, 0xcd8c4692, 0xee9aa39d);
  lmul_(0);
  expect("lmul regression low", cpuState.areg, 0x6d283f7f);
  expect("lmul regression high", cpuState.breg, 0xc6346081);
}

static void testRegisterOperations() {
  reset(0xffffffff, 0, 0x1234);
  gt_(0);
  expect("gt signed 0 > -1", cpuState.areg, 1);
  expect("gt stack", cpuState.breg, 0x1234);
  reset(0, 0x80000000, 0x1234);
  gt_(0);
  expect("gt signed minimum > 0", cpuState.areg, 0);

  for (uint32_t value : {0U, 0x7fffffffU, 0x80000000U, 0xffffffffU}) {
    reset(value, 0x1234, 0x5678);
    xdble_(0);
    expect("xdble low", cpuState.areg, value);
    expect("xdble sign", cpuState.breg,
           value & 0x80000000U ? 0xffffffffU : 0U);
    expect("xdble stack", cpuState.creg, 0x1234);
  }

  for (uint32_t bit = 0; bit < 32; ++bit) {
    reset(1U << bit, 7, 0x1234);
    bitcnt_(0);
    expect("bitcnt individual bit", cpuState.areg, 8);
    expect("bitcnt stack", cpuState.breg, 0x1234);
  }
  reset(0xffffffff, 7, 0);
  bitcnt_(0);
  expect("bitcnt all bits", cpuState.areg, 39);
  reset(0xffffffff, 0xffffffff, 0);
  bitcnt_(0);
  expect("bitcnt wraps", cpuState.areg, 31);

  struct WordOffsetCase { uint32_t offset; uint32_t words; };
  for (const WordOffsetCase &entry : {WordOffsetCase{0, 0}, {3, 0}, {4, 1},
      {0x7fffffff, 0x1fffffff}, {0x80000000, 0xe0000000},
      {0x80000003, 0xe0000000}, {0xfffffffc, 0xffffffff}, {0xffffffff, 0xffffffff}}) {
    reset(entry.offset, 0x1234, 0x5678);
    expect("wcnt succeeds", wcnt_(0) != 0, 0);
    expect("wcnt signed word offset", cpuState.areg, entry.words);
    expect("wcnt byte selector", cpuState.breg, entry.offset & ByteSelectMask);
    expect("wcnt retains old B in C", cpuState.creg, 0x1234);
  }

  for (uint32_t low = 0; low <= 0xffff; ++low) {
    reset(0xdead0000U | low, 0x1234, 0x5678);
    xsword_(0);
    uint32_t extended = low & 0x8000U ? low | 0xffff0000U : low;
    expect("xsword every sixteen-bit value", cpuState.areg, extended);
    expect("xsword preserves B", cpuState.breg, 0x1234);
    expect("xsword preserves C", cpuState.creg, 0x5678);
  }

  reset(0, 2, 0x1234);
  div_(0);
  expect("div zero preserves C in B", cpuState.breg, 0x1234);
  reset(0xffffffff, 0x80000000, 0x1234);
  div_(0);
  expect("div overflow preserves C in B", cpuState.breg, 0x1234);
  reset(3, 0xfffffff6, 0x1234);
  div_(0);
  expect("div signed truncation", cpuState.areg, 0xfffffffd);
  expect("div valid stack", cpuState.breg, 0x1234);
}

static void testCheckInstructions() {
  struct Case { int (*handler)(uint32_t); uint32_t bytes; unsigned int pops; };
  const Case cases[] = {{cb_, 0xfa2b, 0}, {cbu_, 0xfb2b, 0},
         {cs_, 0xfa2f, 0}, {csu_, 0xfb2f, 0},
         {csngl_, 0xfc24, 0}, {cflerr_, 0xf327, 0},
         {ccnt1_, 0xfd24, 1}, {csub0_, 0xf321, 1},
         {cword_, 0xf625, 1}, {cir_, 0xf72c, 2}, {ciru_, 0xfc2c, 2}};
  const uint32_t boundaries[] = {0, 1, 127, 128, 255, 256, 32767, 32768,
      65535, 65536, 0x7f7fffff, 0x7f800000, 0x7f800001, 0x7fffffff,
      0x80000000, 0xffff7fff, 0xffff8000, 0xffffff7f, 0xffffff80, 0xffffffff};
  uint32_t seed = 0x87654321;
  for (const Case &entry : cases) {
    for (uint32_t iteration = 0; iteration < 1024; ++iteration) {
      uint32_t areg = iteration < 400 ? boundaries[iteration / 20] : nextWord(seed);
      uint32_t breg = iteration < 400 ? boundaries[iteration % 20] : nextWord(seed);
      uint32_t creg = iteration < 400 ? boundaries[(iteration / 20 + iteration % 20) % 20] : nextWord(seed);
      if (entry.handler == cword_) areg = 1U << (iteration % 32);
      int64_t signedA = static_cast<int32_t>(areg);
      int64_t signedB = static_cast<int32_t>(breg);
      int64_t signedC = static_cast<int32_t>(creg);
      bool error = false;
      if (entry.handler == cb_) error = signedA < -128 || signedA > 127;
      else if (entry.handler == cbu_) error = signedA < 0 || signedA > 255;
      else if (entry.handler == cs_) error = signedA < -32768 || signedA > 32767;
      else if (entry.handler == csu_) error = signedA < 0 || signedA > 65535;
      else if (entry.handler == csngl_) error = signedB != (signedA < 0 ? -1 : 0);
      else if (entry.handler == cflerr_) error = ((areg >> 23) & 255) == 255;
      else if (entry.handler == ccnt1_) error = breg == 0 || breg > areg;
      else if (entry.handler == csub0_) error = breg >= areg;
      else if (entry.handler == cword_) error = signedB < -static_cast<int64_t>(areg) || signedB >= areg;
      else if (entry.handler == cir_) error = signedC < signedA || signedC > signedB;
      else if (entry.handler == ciru_) error = creg < areg || creg > breg;
      reset(areg, breg, creg);
      cpuState.priority = iteration % 2 == 0 ? HIGH_PRIORITY : LOW_PRIORITY;
      cpuState.iptr = 0x6200;
      storeBytes(0x6200, 2, entry.bytes);
      expect("check instruction decode", decodeNextInstr() != 0, 0);
      expect("check instruction dispatch", instrEntry[instrCode].function == entry.handler, 1);
      int watchFlag = 0;
      expect("check instruction executes", execInstr(&watchFlag) != 0, 0);
      expect("check instruction current error flag",
        cpuState.priority == HIGH_PRIORITY ? omrState.HP_ErrorFlag : omrState.LP_ErrorFlag, error);
      expect("check instruction other priority flag",
        cpuState.priority == HIGH_PRIORITY ? omrState.LP_ErrorFlag : omrState.HP_ErrorFlag, 0);
      expect("check instruction A", cpuState.areg,
        entry.pops == 0 ? areg : entry.pops == 1 ? breg : creg);
      expect("check instruction B", cpuState.breg,
        entry.handler == csngl_ ? creg : entry.pops == 0 ? breg : entry.pops == 1 ? creg : undefinedWord);
      expect("check instruction C", cpuState.creg,
        entry.handler == csngl_ ? undefinedWord : entry.pops == 0 ? creg : undefinedWord);
    }
  }
  for (uint32_t bit = 0; bit < 32; ++bit) {
    uint32_t bound = 1U << bit;
    for (int64_t value : {-static_cast<int64_t>(bound) - 1, -static_cast<int64_t>(bound),
           static_cast<int64_t>(bound) - 1, static_cast<int64_t>(bound)}) {
      if (value < INT32_MIN || value > INT32_MAX) continue;
      reset(bound, static_cast<uint32_t>(value), 0x1234);
      cword_(0);
      expect("cword exact signed boundaries", omrState.HP_ErrorFlag | omrState.LP_ErrorFlag,
        value < -static_cast<int64_t>(bound) || value >= bound);
    }
  }
}

static void testGlobalInterrupts() {
  expect("global enable lowest bit", LP_PROCESS_INT_ENB, 1U << 16);
  expect("global enable highest bit", HP_TIMER_ALRM_ENB, 1U << 23);
  for (uint32_t mask = 0; mask < 256; ++mask) {
    reset(0xdead0000U | mask, 0x1234, 0x5678);
    omrState.Enables = 0xffa5cdef;
    gintenb_(0);
    expect("gintenb old field", cpuState.areg, 0xffa5);
    expect("gintenb packed register", omrState.Enables, 0xffa5cdefU | (mask << 16));
    expect("gintenb preserves B", cpuState.breg, 0x1234);
    expect("gintenb preserves C", cpuState.creg, 0x5678);
    reset(mask, 0x1234, 0x5678);
    omrState.Enables = 0xffa5cdef;
    gintdis_(0);
    expect("gintdis old field", cpuState.areg, 0xffa5);
    expect("gintdis packed register", omrState.Enables, 0xffa5cdefU & ~(mask << 16));
  }
  for (uint32_t mask : {0x100U, 0x8000U, 0xffffU, 0x10000U, 0xffffffffU}) {
    reset(mask, 0x1234, 0x5678);
    omrState.Enables = 0xa55acdef;
    expect("gintenb full field succeeds", gintenb_(0) != 0, 0);
    expect("gintenb full previous field", cpuState.areg, 0xa55a);
    expect("gintenb full field mask", omrState.Enables, 0xa55acdefU | ((mask & 0xffffU) << 16));
    reset(mask, 0x1234, 0x5678);
    omrState.Enables = 0xa55acdef;
    expect("gintdis full field succeeds", gintdis_(0) != 0, 0);
    expect("gintdis full previous field", cpuState.areg, 0xa55a);
    expect("gintdis full field mask", omrState.Enables, 0xa55acdefU & ~((mask & 0xffffU) << 16));
  }
}

static void testProductIdDispatch() {
  reset(0, 0x1234, 0x5678);
  cpuState.iptr = 0x4000;
  storeBytes(0x4000, 3, 0xfc2721);
  expect("lddevid decode", static_cast<uint32_t>(decodeNextInstr()), 0);
  expect("lddevid handler", instrEntry[instrCode].function == lddevid_, 1);
  expect("lddevid executes", static_cast<uint32_t>(instrEntry[instrCode].function(0)), 0);
  expect("lddevid product ID", cpuState.areg, ST20_PRODUCT_ID);
  cpuState.iptr = 0x4004;
  storeBytes(0x4004, 2, 0xfc68);
  expect("ldprodid decode", static_cast<uint32_t>(decodeNextInstr()), 0);
  expect("ldprodid handler", instrEntry[instrCode].function == ldprodid_, 1);
  uint32_t savedProduct = st20ProductId;
  for (uint32_t product : {0U, 0x05200011U, 0x80000001U, 0xffffffffU}) {
    st20ProductId = product;
    for (auto handler : {ldprodid_, lddevid_}) {
      reset(0x1234, 0x5678, 0x9abc);
      expect("configured ID getter succeeds", handler(0) != 0, 0);
      expect("configured ID getter value", cpuState.areg, product);
      expect("configured ID getter pushes old A", cpuState.breg, 0x1234);
      expect("configured ID getter pushes old B", cpuState.creg, 0x5678);
    }
  }
  st20ProductId = savedProduct;
}

static void testMultiplyAndShifts() {
  reset(0xffffffff, 0xffffffff, 0x1234);
  prod_(0);
  expect("prod full-width operands", cpuState.areg, 1);
  expect("prod stack", cpuState.breg, 0x1234);
  reset(0xffffffff, 0xffffffff, 0x1234);
  mul_(0);
  expect("mul signed operands", cpuState.areg, 1);
  reset(0x80000000, 0x80000000, 0);
  mul_(0);
  expect("mul overflowing signed result wraps", cpuState.areg, 0);

  uint32_t seed = 0x87654321;
  for (uint32_t count = 0; count < 64; ++count) {
    for (int iteration = 0; iteration < 8; ++iteration) {
      uint32_t low = nextWord(seed);
      uint32_t high = nextWord(seed);
      uint64_t value = (static_cast<uint64_t>(high) << 32) | low;
      reset(count, low, high);
      lshl_(0);
      uint64_t shifted = value << count;
      expect("lshl low", cpuState.areg, static_cast<uint32_t>(shifted));
      expect("lshl high", cpuState.breg, shifted >> 32);
      reset(count, low, high);
      lshr_(0);
      shifted = value >> count;
      expect("lshr low", cpuState.areg, static_cast<uint32_t>(shifted));
      expect("lshr high", cpuState.breg, shifted >> 32);
      if (count < 32) {
        reset(count, low, high);
        shl_(0);
        expect("shl value", cpuState.areg, low << count);
        expect("shl stack", cpuState.breg, high);
        reset(count, low, high);
        shr_(0);
        expect("shr value", cpuState.areg, low >> count);
        expect("shr stack", cpuState.breg, high);
      }
    }
  }
  for (uint32_t count : {32U, 63U, 64U, 96U, 65536U, 0xffffffffU}) {
    reset(count, 0xffffffff, 0x1234);
    shl_(0);
    expect("shl out-of-range policy", cpuState.areg, UNDEFINED_WORD_OLD);
    expect("shl out-of-range stack", cpuState.breg, 0x1234);
    reset(count, 0xffffffff, 0x1234);
    shr_(0);
    expect("shr out-of-range policy", cpuState.areg, UNDEFINED_WORD_OLD);
    if (count >= 64) {
      reset(count, 0xffffffff, 0x1234);
      lshl_(0);
      expect("lshl out-of-range low", cpuState.areg, UNDEFINED_WORD_OLD);
      expect("lshl out-of-range high", cpuState.breg, UNDEFINED_WORD_OLD);
      reset(count, 0xffffffff, 0x1234);
      lshr_(0);
      expect("lshr out-of-range low", cpuState.areg, UNDEFINED_WORD_OLD);
      expect("lshr out-of-range high", cpuState.breg, UNDEFINED_WORD_OLD);
    }
  }
}

static uint32_t reverseBitsReference(uint32_t value, uint32_t count) {
  uint32_t result = 0;
  for (uint32_t bit = 0; bit < count; ++bit)
    result = (result << 1) | ((value >> bit) & 1U);
  return result;
}

static void testBitInstructions() {
  uint32_t seed = 0xabcdef01;
  for (uint32_t count = 0; count <= 32; ++count) {
    for (int iteration = 0; iteration < 64; ++iteration) {
      uint32_t value = nextWord(seed);
      reset(value, 0x1234, 0x5678);
      bitrevword_(0);
      expect("bitrevword", cpuState.areg, reverseBitsReference(value, 32));
      expect("bitrevword preserves B", cpuState.breg, 0x1234);
      expect("bitrevword preserves C", cpuState.creg, 0x5678);
      reset(count, value, 0x1234);
      bitrevnbits_(0);
      expect("bitrevnbits", cpuState.areg, reverseBitsReference(value, count));
      expect("bitrevnbits stack", cpuState.breg, 0x1234);
    }
  }
  for (uint32_t count : {33U, 65536U, 0xffffffffU}) {
    reset(count, 0xffffffff, 0x1234);
    bitrevnbits_(0);
    expect("bitrevnbits undefined policy", cpuState.areg, UNDEFINED_WORD_OLD);
  }
  for (uint32_t value = 0; value <= 255; ++value) {
    reset(0xdeadbe00U | value, 0x1234, 0x5678);
    xbword_(0);
    uint32_t extended = value < 128 ? value : value | 0xffffff00U;
    expect("xbword", cpuState.areg, extended);
    expect("xbword preserves B", cpuState.breg, 0x1234);
    expect("xbword preserves C", cpuState.creg, 0x5678);
  }
  for (uint32_t width = 1; width <= 32; ++width) {
    uint32_t signMask = 1U << (width - 1);
    uint32_t mask = width == 32 ? 0xffffffffU : (1U << width) - 1;
    for (uint32_t value : {0U, signMask - 1, signMask, mask}) {
      reset(signMask, value, 0x1234);
      xword_(0);
      uint32_t extended = (value & signMask) == 0 ? value : value | ~mask;
      expect("xword", cpuState.areg, extended);
      expect("xword stack", cpuState.breg, 0x1234);
    }
  }
  for (uint32_t signMask : {0U, 3U, 0xffffffffU}) {
    reset(signMask, 0, 0);
    xword_(0);
    expect("xword invalid mask policy", cpuState.areg, UNDEFINED_WORD_OLD);
  }
  reset(0x80, 0x100, 0);
  xword_(0);
  expect("xword too many bits policy", cpuState.areg, UNDEFINED_WORD_OLD);

  for (uint32_t value : {0U, 1U, 0x7fffU, 0x8000U, 0xffffU}) {
    storeBytes(0x7fff, 2, static_cast<uint32_t>(value));
    reset(0x7fff, 0x1234, 0x5678);
    expect("lsx success", static_cast<uint32_t>(lsx_(0)), 0);
    uint32_t extended = value < 0x8000 ? value : value | 0xffff0000U;
    expect("lsx sign extension", cpuState.areg, extended);
    expect("lsx preserves B", cpuState.breg, 0x1234);
    expect("lsx preserves C", cpuState.creg, 0x5678);
  }
  reset(0x70000000, 0, 0);
  expect("lsx unreadable memory", lsx_(0) != 0, 1);
}

static void testNormalization() {
  reset(0, 0, 0x1234);
  norm_(0);
  expect("norm zero low", cpuState.areg, 0);
  expect("norm zero high", cpuState.breg, 0);
  expect("norm zero count", cpuState.creg, 64);
  for (uint32_t bit = 0; bit < 64; ++bit) {
    uint64_t value = uint64_t{1} << bit;
    reset(static_cast<uint32_t>(value), static_cast<uint32_t>(value >> 32), 0);
    norm_(0);
    expect("norm single bit low", cpuState.areg, 0);
    expect("norm single bit high", cpuState.breg, 0x80000000);
    expect("norm single bit count", cpuState.creg, 63 - bit);
  }
  uint32_t seed = 0x31415926;
  for (int iteration = 0; iteration < 512; ++iteration) {
    uint32_t low = nextWord(seed);
    uint32_t high = nextWord(seed);
    uint64_t normalized = (static_cast<uint64_t>(high) << 32) | low;
    uint32_t count = 0;
    while (normalized != 0 && (normalized & (uint64_t{1} << 63)) == 0) {
      normalized <<= 1;
      ++count;
    }
    if (normalized == 0) count = 64;
    reset(low, high, 0x1234);
    norm_(0);
    expect("norm random low", cpuState.areg, static_cast<uint32_t>(normalized));
    expect("norm random high", cpuState.breg, normalized >> 32);
    expect("norm random count", cpuState.creg, count);
  }
}

static void testIndependentDispatch() {
  struct Case { uint32_t bytes; int (*handler)(uint32_t); };
  const Case cases[] = {{0xf727, bitrevword_}, {0xf827, bitrevnbits_},
                        {0xf82b, xbword_}, {0xfa23, xword_},
                        {0xf921, norm_}, {0xf92f, lsx_},
                        {0xf527, crcbyte_}, {0xf427, crcword_},
                        {0xf826, satadd_}, {0xf926, satsub_}, {0xfa26, satmul_},
                        {0xf426, slmul_}, {0xf526, sulmul_},
                        {0xf127, ldinf_}, {0xf725, clrhalterr_},
                        {0xf825, sethalterr_}, {0xf925, testhalterr_}, {0xf122, lend_},
                        {0xfb22, tin_}, {0xf160, swaptimer_},
                        {0xf460, signal_}, {0xf560, wait_},
                        {0xf7, in_}, {0xfb, out_}, {0xfe, outbyte_}, {0xff, outword_},
                        {0xfb25, move2dinit_}, {0xfc25, move2dall_},
                        {0xfd25, move2dnonzero_}, {0xfe25, move2dzero_},
                        {0xff62, causeerror_}, {0xfb60, tret_}, {0xf62c, ldtrapped_},
                        {0xff26, sttraph_}, {0xfb2c, sttrapped_}, {0xf021, seterr_},
                        {0xf922, testerr_}, {0xf525, stoperr_}, {0xff21, rem_},
                        {0xf324, alt_}, {0xf424, altwt_}, {0xf524, altend_},
                        {0xfe24, talt_}, {0xf125, taltwt_}, {0xf824, enbc_},
                        {0xf924, enbs_}, {0xf724, enbt_}, {0xff22, disc_},
                        {0xf023, diss_}, {0xfe22, dist_},
                        {0xfd23, savel_}, {0xfe23, saveh_}, {0xf025, sthb_},
                        {0xf821, sthf_}, {0xf721, stlb_}, {0xfc21, stlf_},
                        {0xf060, swapqueue_}, {0xf260, insertqueue_},
                        {0xf227, fmul_}, {0xfc29, fptesterr_}, {0xf3, endp_},
                        {0xf326, unpacksn_}, {0xfa22, testpranal_},
                        {0xf42c, intdis_}, {0xf52c, intenb_},
                        {0xf02b, settimeslice_}, {0xf360, timeslice_}, {0xff61, iret_},
                        {0xfc60, ldshadow_}, {0xfd60, stshadow_}, {0xf462, devmove_},
                        {0xfc26, postnormsn_}, {0xfd26, roundsn_}, {0xfe62, restart_}};
  for (const Case &entry : cases) {
    cpuState.iptr = 0x6000;
    storeBytes(0x6000, 2, static_cast<uint32_t>(entry.bytes));
    expect("independent instruction decode", static_cast<uint32_t>(decodeNextInstr()), 0);
    expect("independent instruction handler", instrEntry[instrCode].function == entry.handler, 1);
  }
}

static uint32_t crcReference(uint32_t data, uint32_t checksum, uint32_t polynomial, uint32_t count) {
  uint64_t dividend = (static_cast<uint64_t>(checksum) << count) |
                      (count == 32 ? data : data >> 24);
  uint64_t divisor = (uint64_t{1} << 32) | polynomial;
  for (uint32_t position = 32 + count; position-- > 32;) {
    if ((dividend & (uint64_t{1} << position)) != 0)
      dividend ^= divisor << (position - 32);
  }
  return static_cast<uint32_t>(dividend);
}

static void testCrcInstructions() {
  uint32_t seed = 0x12345678;
  for (uint32_t polynomial : {0U, 1U, 0x04c11db7U, 0x80000000U, 0xffffffffU}) {
    for (int iteration = 0; iteration < 256; ++iteration) {
      uint32_t data = nextWord(seed);
      uint32_t checksum = nextWord(seed);
      reset(data, checksum, polynomial);
      crcbyte_(0);
      expect("crcbyte polynomial division", cpuState.areg,
             crcReference(data, checksum, polynomial, 8));
      expect("crcbyte preserves polynomial in B", cpuState.breg, polynomial);
      reset(data, checksum, polynomial);
      crcword_(0);
      expect("crcword polynomial division", cpuState.areg,
             crcReference(data, checksum, polynomial, 32));
      expect("crcword preserves polynomial in B", cpuState.breg, polynomial);
    }
  }
  reset(0xaa000001, 0xffffffff, 0x04c11db7);
  crcbyte_(0);
  uint32_t result = cpuState.areg;
  reset(0xaaffffff, 0xffffffff, 0x04c11db7);
  crcbyte_(0);
  expect("crcbyte ignores lower three bytes", cpuState.areg, result);
  reset(0x80000000, 0, 0);
  crcbyte_(0);
  expect("crcbyte consumes most significant byte", cpuState.areg, 0x80);
}

static uint32_t saturationReference(int64_t value) {
  if (value > 2147483647) return 0x7fffffff;
  if (value < -2147483648LL) return 0x80000000;
  return static_cast<uint32_t>(value);
}

static void testSaturatingInstructions() {
  const int32_t values[] = {-2147483647 - 1, -2147483647, -2, -1, 0, 1, 2, 2147483647};
  for (int32_t left : values) {
    for (int32_t right : values) {
      reset(static_cast<uint32_t>(right), static_cast<uint32_t>(left), 0x1234);
      omrState.HP_ErrorFlag = 1;
      satadd_(0);
      expect("satadd clamp", cpuState.areg,
             saturationReference(static_cast<int64_t>(left) + right));
      expect("satadd stack", cpuState.breg, 0x1234);
      expect("satadd keeps error flag", omrState.HP_ErrorFlag, 1);
      reset(static_cast<uint32_t>(right), static_cast<uint32_t>(left), 0x1234);
      satsub_(0);
      expect("satsub clamp", cpuState.areg,
             saturationReference(static_cast<int64_t>(left) - right));
      expect("satsub stack", cpuState.breg, 0x1234);
      expect("satsub no arithmetic error", omrState.HP_ErrorFlag | omrState.LP_ErrorFlag, 0);
      reset(static_cast<uint32_t>(right), static_cast<uint32_t>(left), 0x1234);
      satmul_(0);
      expect("satmul clamp", cpuState.areg,
             saturationReference(static_cast<int64_t>(left) * right));
      expect("satmul stack", cpuState.breg, 0x1234);
      expect("satmul no arithmetic error", omrState.HP_ErrorFlag | omrState.LP_ErrorFlag, 0);
    }
  }
}

static void testSignedLongProducts() {
  const uint32_t values[] = {0, 1, 0x7fffffff, 0x80000000, 0xfffffffe, 0xffffffff};
  for (uint32_t areg : values) {
    for (uint32_t breg : values) {
      for (uint32_t carry : {0U, 1U, 0x80000000U, 0xffffffffU}) {
        uint64_t signedA = static_cast<uint64_t>(static_cast<int32_t>(areg));
        uint64_t signedB = static_cast<uint64_t>(static_cast<int32_t>(breg));
        uint64_t product = signedA * signedB + carry;
        reset(areg, breg, carry);
        slmul_(0);
        expect("slmul low", cpuState.areg, static_cast<uint32_t>(product));
        expect("slmul high", cpuState.breg, product >> 32);
        product = signedA * breg + carry;
        reset(areg, breg, carry);
        sulmul_(0);
        expect("sulmul low", cpuState.areg, static_cast<uint32_t>(product));
        expect("sulmul high", cpuState.breg, product >> 32);
      }
    }
  }
  reset(0xffffffff, 1, 0);
  slmul_(0);
  expect("slmul negative low word", cpuState.areg, 0xffffffff);
  expect("slmul negative high word", cpuState.breg, 0xffffffff);
}

static void testFractionMultiply() {
  const uint32_t boundaries[] = {0, 1, 2, 3, 0x3fffffff, 0x40000000, 0x40000001,
      0x7ffffffe, 0x7fffffff, 0x80000000, 0x80000001, 0xbfffffff, 0xc0000000,
      0xc0000001, 0xfffffffd, 0xfffffffe, 0xffffffff};
  uint32_t seed = 0x31415926;
  for (uint32_t iteration = 0; iteration < 4096; ++iteration) {
    uint32_t areg = iteration < 289 ? boundaries[iteration / 17] : nextWord(seed);
    uint32_t breg = iteration < 289 ? boundaries[iteration % 17] : nextWord(seed);
    int64_t product = static_cast<int64_t>(static_cast<int32_t>(areg)) * static_cast<int32_t>(breg);
    int64_t divisor = int64_t{1} << 31;
    int64_t quotient = product / divisor;
    int64_t remainder = product % divisor;
    int64_t distance = remainder < 0 ? -remainder : remainder;
    if (distance * 2 > divisor || (distance * 2 == divisor && quotient % 2 != 0))
      quotient += product < 0 ? -1 : 1;
    bool overflow = areg == 0x80000000U && breg == 0x80000000U;
    reset(areg, breg, 0x12345678);
    cpuState.priority = iteration % 2 == 0 ? HIGH_PRIORITY : LOW_PRIORITY;
    expect("fmul executes", fmul_(0) != 0, 0);
    expect("fmul nearest-even product", cpuState.areg,
           overflow ? undefinedWord : static_cast<uint32_t>(quotient));
    expect("fmul B stack", cpuState.breg, 0x12345678);
    expect("fmul C undefined", cpuState.creg, undefinedWord);
    expect("fmul overflow flag", omrState.HP_ErrorFlag | omrState.LP_ErrorFlag, overflow);
  }
  struct TieCase { uint32_t value; uint32_t rounded; };
  for (const TieCase &entry : {TieCase{1, 0}, TieCase{3, 2}, TieCase{5, 2},
                              TieCase{0xffffffff, 0}, TieCase{0xfffffffd, 0xfffffffe},
                              TieCase{0xfffffffb, 0xfffffffe}}) {
    reset(entry.value, 0x40000000, 0x1234);
    fmul_(0);
    expect("fmul explicit signed half ties", cpuState.areg, entry.rounded);
  }
  reset(0x80000000, 0x80000000, 0x1234);
  undefinedWord = 0xa5a5a5a5;
  fmul_(0);
  expect("fmul configured undefined overflow result", cpuState.areg, 0xa5a5a5a5);
  undefinedWord = UNDEFINED_WORD_OLD;
  reset(0x1234, 0x5678, 0x9abc);
  omrState.HP_ErrorFlag = 1;
  omrState.LP_ErrorFlag = 1;
  expect("fptesterr no FPU succeeds", fptesterr_(0) != 0, 0);
  expect("fptesterr no FPU true", cpuState.areg, 1);
  expect("fptesterr pushes previous A", cpuState.breg, 0x1234);
  expect("fptesterr pushes previous B", cpuState.creg, 0x5678);
  expect("fptesterr preserves high integer error", omrState.HP_ErrorFlag, 1);
  expect("fptesterr preserves low integer error", omrState.LP_ErrorFlag, 1);
  reset(3, 0x40000000, 0x9abc);
  cpuState.running = true;
  cpuState.iptr = 0x7b00;
  storeBytes(0x7b00, 2, 0xf227);
  expect("encoded fmul decode", decodeNextInstr() != 0, 0);
  int watchFlag = 0;
  expect("encoded fmul executes", execInstr(&watchFlag) != 0, 0);
  expect("encoded fmul nearest-even result", cpuState.areg, 2);
  cpuState.iptr = 0x7b00;
  storeBytes(0x7b00, 2, 0xfc29);
  expect("encoded fptesterr decode", decodeNextInstr() != 0, 0);
  expect("encoded fptesterr executes", execInstr(&watchFlag) != 0, 0);
  expect("encoded fptesterr pushes true", cpuState.areg, 1);
  expect("encoded fptesterr shifts product", cpuState.breg, 2);
}

static void testUnpackSingle() {
  const uint32_t fractions[] = {0, 1, 0x003fffff, 0x00400000, 0x007fffff};
  for (uint32_t sign : {0U, 0x80000000U}) {
    for (uint32_t exponent = 0; exponent < 256; ++exponent) {
      for (uint32_t fraction : fractions) {
        for (uint32_t oldBreg : {0U, 1U, 0x3fffffffU, 0x40000000U, 0xffffffffU}) {
          uint32_t packed = sign | (exponent << 23) | fraction;
          uint32_t type = 1;
          if (exponent == 0 && fraction == 0) type = 0;
          if (exponent == 255) type = fraction == 0 ? 2U : 3U;
          reset(packed, oldBreg, 0x12345678);
          expect("unpacksn executes", unpacksn_(0) != 0, 0);
            expect("unpacksn aligned significand", cpuState.areg, exponent == 255 ? undefinedWord
              : (fraction << 8) | (exponent != 0 ? WordSignBit : 0U));
            expect("unpacksn exponent with denormal minimum", cpuState.breg,
               exponent == 0 && fraction != 0 ? 1U : exponent);
          expect("unpacksn tagged previous B", cpuState.creg,
                 static_cast<uint32_t>(static_cast<uint64_t>(oldBreg) * 4 + type));
          expect("unpacksn has no integer error", omrState.HP_ErrorFlag | omrState.LP_ErrorFlag, 0);
        }
      }
    }
  }
  reset(0xffc12345, 0x12345678, 0x9abc);
  cpuState.running = true;
  cpuState.iptr = 0x8500;
  storeBytes(0x8500, 2, 0xf326);
  expect("encoded unpacksn decode", decodeNextInstr() != 0, 0);
  int watchFlag = 0;
  expect("encoded unpacksn executes", execInstr(&watchFlag) != 0, 0);
  expect("encoded unpacksn NaN significand undefined", cpuState.areg, undefinedWord);
  expect("encoded unpacksn exponent", cpuState.breg, 255);
  expect("encoded unpacksn NaN tag", cpuState.creg, 0x48d159e3);
}

static void testNativeFloatHelpers() {
  setPortableInstructionAbi(false);
  struct RoundCase { uint32_t guard; uint32_t fraction; uint32_t exponent; uint32_t expected; };
  const RoundCase rounding[] = {
      {0, 0, 0, 0}, {0, 0x80000000, 127, 0x3f800000},
      {0, 0xc0000000, 127, 0x3fc00000}, {0, 0x80000080, 127, 0x3f800000},
      {1, 0x80000080, 127, 0x3f800001}, {0, 0x80000180, 127, 0x3f800002},
      {0, 0xffffff80, 254, 0x7f800000}, {0, 0x80000000, 255, 0x7f800000},
      {0, 0x7fffff80, 0, 0x00800000}, {0, 0x00000080, 0, 0},
      {1, 0x00000080, 0, 1}, {0xffffffff, 0x7fffffff, 0xffffffff, 0}};
  struct NormalizeCase {
    uint32_t guard; uint32_t fraction; uint32_t shift; uint32_t temporary;
    uint32_t expectedGuard; uint32_t expectedFraction; uint32_t expectedExponent;
  };
  const NormalizeCase normalization[] = {
      {0, 0x80000000, 0, 127, 0, 0x80000000, 127},
      {1, 0x80000080, 1, 127, 1, 0x80000080, 126},
      {0, 0x80000000, 0, 0, 0, 0x40000000, 0},
      {0, 0x80000000, 0, 0xffffffe9, 0, 0x80, 0},
      {1, 0x8000007f, 0, 0xffffffe9, 0x7f01, 0x80, 0},
      {0xffffffff, 0xffffffff, 0, 0xffffffe8, 0, 0, 0},
      {0x2dd23154, 0x94e811a8, 23, 21, 0x2dfa777e, 0x129d0235, 0},
      {0x80000000, 0xffffffff, 14, 0x80000000, 0x80000000, 0xffffffff, 255},
      {0, 0, 64, 64, 0, 0, 0}};
  for (unsigned char priority : {static_cast<unsigned char>(HIGH_PRIORITY), static_cast<unsigned char>(LOW_PRIORITY)}) {
    cpuState.priority = priority;
    cpuState.wptr = 0x1c1000;
    for (const auto &entry : rounding) {
      reset(entry.guard, entry.fraction, entry.exponent);
      expect("native roundsn executes", roundsn_(0) != 0, 0);
      expect("native roundsn packed value", cpuState.areg, entry.expected);
      expect("native roundsn B undefined", cpuState.breg, undefinedWord);
      expect("native roundsn C undefined", cpuState.creg, undefinedWord);
      expect("native roundsn no integer error", omrState.HP_ErrorFlag | omrState.LP_ErrorFlag, 0);
    }
    for (const auto &entry : normalization) {
      reset(entry.guard, entry.fraction, entry.shift);
      storeBytes(cpuState.wptr, 4, entry.temporary);
      expect("native postnormsn executes", postnormsn_(0) != 0, 0);
      expect("native postnormsn guard", cpuState.areg, entry.expectedGuard);
      expect("native postnormsn fraction", cpuState.breg, entry.expectedFraction);
      expect("native postnormsn exponent", cpuState.creg, entry.expectedExponent);
      uint32_t temporary = 0;
      readBytes(cpuState.wptr, 4, &temporary);
      expect("native postnormsn preserves workspace", temporary, entry.temporary);
    }
  }
  uint32_t seed = 0x892fe314;
  for (int iteration = 0; iteration < 1024; ++iteration) {
    uint64_t significand = (static_cast<uint64_t>(nextWord(seed) | 0x80000000U) << 32) | nextWord(seed);
    significand &= ~uint64_t{0x7ff};
    uint32_t exponent = nextWord(seed) % 255U;
    if (exponent == 0) significand &= ~(uint64_t{1} << 63);
    float expected = static_cast<float>(std::ldexp(static_cast<double>(significand),
        static_cast<int>(exponent == 0 ? 1 : exponent) - 190));
    reset(static_cast<uint32_t>(significand), static_cast<uint32_t>(significand >> 32), exponent);
    roundsn_(0);
    expect("native roundsn independent IEEE conversion", cpuState.areg, std::bit_cast<uint32_t>(expected));
  }
  reset(0x1234, 0x5678, 3);
  cpuState.wptr = 0x6c001234;
  expect("native postnormsn reports read failure", postnormsn_(0) == READ_UNUSED_MEM, 1);
  expect("native postnormsn failed read preserves A", cpuState.areg, 0x1234);
  expect("native postnormsn failed read preserves B", cpuState.breg, 0x5678);
  expect("native postnormsn failed read preserves C", cpuState.creg, 3);
  reset(1, 0x80000080, 0);
  cpuState.priority = HIGH_PRIORITY;
  cpuState.wptr = 0x1c1000;
  cpuState.running = true;
  cpuState.iptr = 0x8600;
  storeBytes(cpuState.wptr, 4, 127);
  storeBytes(0x8600, 4, 0xfd26fc26);
  int watchFlag = 0;
  expect("native encoded postnormsn decode", decodeNextInstr() != 0, 0);
  expect("native encoded postnormsn executes", execInstr(&watchFlag) != 0, 0);
  expect("native encoded postnormsn biased exponent", cpuState.creg, 127);
  expect("native encoded roundsn decode", decodeNextInstr() != 0, 0);
  expect("native encoded roundsn executes", execInstr(&watchFlag) != 0, 0);
  expect("native encoded FP sequence", cpuState.areg, 0x3f800001);
}

static void testPortableFloatHelpers() {
  setPortableInstructionAbi(true);
  struct Case { uint64_t significand; int32_t exponent; uint32_t expected; };
  const Case cases[] = {{0x8000000000000000ULL, 0, 0x3f800000},
      {0xc000000000000000ULL, 0, 0x3fc00000}, {0x8000000000000000ULL, -1, 0x3f000000},
      {0x8000008000000000ULL, 0, 0x3f800000}, {0x8000008000000001ULL, 0, 0x3f800001},
      {0x8000018000000000ULL, 0, 0x3f800002}, {0xffffffffffffffffULL, 127, 0x7f800000},
      {0x8000000000000000ULL, -126, 0x00800000}, {0xffffffffffffffffULL, -127, 0x00800000},
      {0x8000000000000000ULL, -149, 1}, {0x8000000000000000ULL, -150, 0},
      {0x8000000000000001ULL, -150, 1}, {0x8000000000000000ULL, -151, 0},
      {0, 0, 0}};
  for (uint32_t sign : {0U, 0x80000000U}) {
    for (const Case &entry : cases) {
      reset(static_cast<uint32_t>(entry.significand), static_cast<uint32_t>(entry.significand >> 32),
            sign | (static_cast<uint32_t>(entry.exponent) & 0x7fffffffU));
      expect("portable roundsn succeeds", roundsn_(0) != 0, 0);
      expect("portable roundsn IEEE packing", cpuState.areg, sign | entry.expected);
      expect("portable roundsn B undefined", cpuState.breg, undefinedWord);
      expect("portable roundsn C undefined", cpuState.creg, undefinedWord);
    }
  }
  uint32_t seed = 0x456789ab;
  for (int iteration = 0; iteration < 1024; ++iteration) {
    uint64_t significand = (static_cast<uint64_t>(nextWord(seed) | 0x80000000U) << 32) | nextWord(seed);
    significand &= ~uint64_t{0x7ff};
    int32_t exponent = static_cast<int32_t>(nextWord(seed) % 280U) - 150;
    float expected = static_cast<float>(std::ldexp(static_cast<double>(significand), exponent - 63));
    reset(static_cast<uint32_t>(significand), static_cast<uint32_t>(significand >> 32),
          static_cast<uint32_t>(exponent) & 0x7fffffffU);
    roundsn_(0);
    expect("portable roundsn independent IEEE conversion", cpuState.areg, std::bit_cast<uint32_t>(expected));
  }
  cpuState.wptr = 0x1c0000;
  storeBytes(0x1c0000, 4, 0);
  reset(0, 0x40000000, 0);
  norm_(0);
  expect("portable postnormsn executes", postnormsn_(0) != 0, 0);
  expect("portable postnormsn adjusts exponent", cpuState.creg, 0x7fffffff);
  roundsn_(0);
  expect("portable norm-round sequence", cpuState.areg, 0x3f000000);
  reset(1, 2, 65);
  expect("portable postnormsn rejects invalid shift", postnormsn_(0) == INVALID_PORTABLE_STATE, 1);
  reset(1, 2, 0);
  expect("portable roundsn rejects nonnormalized input", roundsn_(0) == INVALID_PORTABLE_STATE, 1);
  setPortableInstructionAbi(false);
}

static void testResetAnalysis() {
  for (bool analysed : {false, true}) {
    omrState = {};
    expect("reset CPU with analyse input", initCPUState(analysed) != 0, 0);
    for (unsigned char priority : {static_cast<unsigned char>(HIGH_PRIORITY), static_cast<unsigned char>(LOW_PRIORITY)}) {
      cpuState.priority = priority;
      cpuState.areg = 0x1234;
      cpuState.breg = 0x5678;
      cpuState.creg = 0x9abc;
      omrState.HP_ErrorFlag = 1;
      omrState.LP_ErrorFlag = 1;
      expect("testpranal succeeds", testpranal_(0) != 0, 0);
      expect("testpranal latched reset input", cpuState.areg, analysed);
      expect("testpranal pushes previous A", cpuState.breg, 0x1234);
      expect("testpranal pushes previous B", cpuState.creg, 0x5678);
      expect("testpranal preserves high error", omrState.HP_ErrorFlag, 1);
      expect("testpranal preserves low error", omrState.LP_ErrorFlag, 1);
      resetScheduler();
      expect("scheduler reset preserves reset analyse latch", omrState.analysedOnReset, analysed);
    }
    cpuState.iptr = 0x8600;
    storeBytes(0x8600, 2, 0xfa22);
    expect("encoded testpranal decode", decodeNextInstr() != 0, 0);
    int watchFlag = 0;
    expect("encoded testpranal executes", execInstr(&watchFlag) != 0, 0);
    expect("encoded testpranal reset input", cpuState.areg, analysed);
  }
  expect("ordinary reset succeeds", initCPUState() != 0, 0);
  expect("ordinary reset clears analyse latch", omrState.analysedOnReset, 0);
  char directory[] = "/tmp/st20-analysis-XXXXXX";
  expect("analyse snapshot temp directory", mkdtemp(directory) != nullptr, 1);
  expect("analysed reset for snapshot", initCPUState(true) != 0, 0);
  expect("save analysed reset snapshot", saveCPUState(directory) != 0, 0);
  omrState.analysedOnReset = false;
  expect("restore analysed reset snapshot", loadCPUState(directory) != 0, 0);
  expect("snapshot preserves reset analyse latch", omrState.analysedOnReset, 1);
  std::string filename = std::string(directory) + "/cpu.bin";
  FILE *file = std::fopen(filename.c_str(), "rb+");
  expect("open reset snapshot for compatibility fixture", file != nullptr, 1);
  if (file) {
    std::fseek(file, 0, SEEK_END);
    int64_t length = std::ftell(file);
    int64_t runtimeStart = length - static_cast<int64_t>(cpuRuntimeSnapshotSize());
    uint32_t invalidFlag = 2;
    std::fseek(file, runtimeStart + static_cast<uint32_t>(169 * sizeof(uint32_t)), SEEK_SET);
    std::fwrite(&invalidFlag, sizeof(invalidFlag), 1, file);
    std::fflush(file);
    expect("reject nonboolean reset latch snapshot", loadCPUState(directory) == INVALID_CPU_FILE, 1);
    expect("invalid reset snapshot leaves latch unchanged", omrState.analysedOnReset, 1);
    uint32_t version = 3;
    std::fseek(file, runtimeStart + static_cast<uint32_t>(sizeof(uint32_t)), SEEK_SET);
    std::fwrite(&version, sizeof(version), 1, file);
    std::fflush(file);
    expect("truncate to version3 runtime", ftruncate(fileno(file), runtimeStart + static_cast<uint32_t>(169 * sizeof(uint32_t))) != 0, 0);
    std::fclose(file);
    expect("reject version3 snapshot", loadCPUState(directory) == INVALID_CPU_FILE, 1);
    expect("version3 rejection preserves reset latch", omrState.analysedOnReset, 1);
  }
  unlink(filename.c_str());
  rmdir(directory);
  omrState = {};
  initCPUState();
}

static void testInfinityAndHaltMode() {
  reset(0x1234, 0x5678, 0x9abc);
  ldinf_(0);
  expect("ldinf IEEE positive infinity", cpuState.areg, 0x7f800000);
  expect("ldinf shifts A into B", cpuState.breg, 0x1234);
  expect("ldinf shifts B into C", cpuState.creg, 0x5678);

  reset(0x1234, 0x5678, 0x9abc);
  omrState.HP_ErrorFlag = 1;
  omrState.LP_ErrorFlag = 1;
  for (int iteration = 0; iteration < 2; ++iteration) {
    sethalterr_(0);
    expect("sethalterr sets mode", omrState.HaltOnErrorFlag, 1);
    expect("sethalterr preserves A", cpuState.areg, 0x1234);
    expect("sethalterr preserves B", cpuState.breg, 0x5678);
    expect("sethalterr preserves C", cpuState.creg, 0x9abc);
    expect("sethalterr preserves HP error", omrState.HP_ErrorFlag, 1);
    expect("sethalterr preserves LP error", omrState.LP_ErrorFlag, 1);
  }
  for (int iteration = 0; iteration < 2; ++iteration) {
    clrhalterr_(0);
    expect("clrhalterr clears mode", omrState.HaltOnErrorFlag, 0);
    expect("clrhalterr preserves A", cpuState.areg, 0x1234);
    expect("clrhalterr preserves B", cpuState.breg, 0x5678);
    expect("clrhalterr preserves C", cpuState.creg, 0x9abc);
    expect("clrhalterr preserves HP error", omrState.HP_ErrorFlag, 1);
    expect("clrhalterr preserves LP error", omrState.LP_ErrorFlag, 1);
  }
  const unsigned char flags[] = {0, 1, 255};
  for (unsigned char flag : flags) {
    reset(0x1234, 0x5678, 0x9abc);
    omrState.HaltOnErrorFlag = flag;
    testhalterr_(0);
    expect("testhalterr boolean", cpuState.areg, flag != 0);
    expect("testhalterr shifts A into B", cpuState.breg, 0x1234);
    expect("testhalterr shifts B into C", cpuState.creg, 0x5678);
    expect("testhalterr preserves mode", omrState.HaltOnErrorFlag, flag);
  }
}

static void testLoopEnd() {
  for (uint32_t count : {0U, 1U, 2U, 3U, 0x7fffffffU, 0x80000000U, 0xffffffffU}) {
    for (uint32_t index : {0U, 0x7fffffffU, 0xffffffffU}) {
      reset(0x20, 0x9000, 0x1234);
      cpuState.iptr = 0x1000;
      storeBytes(0x9000, 4, static_cast<uint32_t>(index));
      storeBytes(0x9004, 4, static_cast<uint32_t>(count));
      expect("lend succeeds", static_cast<uint32_t>(lend_(0)), 0);
      bool repeats = count > 1 && count < 0x80000000U;
      uint32_t actual = 0;
      readBytes(0x9004, 4, &actual);
      expect("lend always decrements count", actual, count - 1U);
      readBytes(0x9000, 4, &actual);
      expect("lend index", actual, repeats ? index + 1 : index);
      expect("lend branch", cpuState.iptr, repeats ? 0xfe0 : 0x1000);
    }
  }

  reset(0x20, 0xfffffffc, 0);
  cpuState.iptr = 0x10;
  storeBytes(0xfffffffc, 4, 0xffffffff);
  storeBytes(0, 4, 2);
  lend_(0);
  uint32_t actual = 0;
  readBytes(0, 4, &actual);
  expect("lend wrapped count address", actual, 1);
  readBytes(0xfffffffc, 4, &actual);
  expect("lend index wraps", actual, 0);
  expect("lend Iptr wraps", cpuState.iptr, 0xfffffff0);

  reset(0x20, 0x9100, 0);
  cpuState.iptr = 0x1000;
  storeBytes(0x9104, 4, 1);
  expect("lend terminal count does not read index", static_cast<uint32_t>(lend_(0)), 0);
  expect("lend terminal count does not branch", cpuState.iptr, 0x1000);
  readBytes(0x9104, 4, &actual);
  expect("lend terminal count becomes zero", actual, 0);

  reset(0x20, 0x9200, 0);
  cpuState.iptr = 0x1000;
  storeBytes(0x9204, 4, 2);
  expect("lend unreadable index", lend_(0) != 0, 1);
  readBytes(0x9204, 4, &actual);
  expect("lend read failure preserves count", actual, 2);
  expect("lend read failure preserves Iptr", cpuState.iptr, 0x1000);
  reset(0x20, 0x70001000, 0);
  expect("lend unreadable count", lend_(0) != 0, 1);

  reset(0x20, 0x9300, 0);
  cpuState.iptr = 0x6010;
  storeBytes(0x6010, 2, 0xf122);
  storeBytes(0x9300, 4, 7);
  storeBytes(0x9304, 4, 3);
  decodeNextInstr();
  int breakFlag = 0;
  expect("lend decoded execution", static_cast<uint32_t>(execInstr(&breakFlag)), 0);
  expect("lend offset from next instruction", cpuState.iptr, 0x5ff2);
}

static void testLegacySnapshot() {
  struct LegacyState {
    uint32_t areg, breg, creg, iptr, status;
    int nWptr;
    bool wptrUsed[MAX_WPTR];
  };
  char directory[] = "/tmp/st20-snapshot-XXXXXX";
  if (!mkdtemp(directory)) {
    expect("snapshot temporary directory", 0, 1);
    return;
  }
  std::string filename = std::string(directory) + "/cpu.bin";
  LegacyState legacy{0x12345678, 0x87654321, 0xabcdef01, 0x6010, 42, 7, {true}};
  uint32_t endAddress = WPTR_END_ADDR;
  WATCH legacyWatch{};
  legacyWatch.watchAreg = true;
  legacyWatch.areg = legacy.areg;
  FILE *file = std::fopen(filename.c_str(), "wb");
  if (!file) {
    expect("snapshot fixture open", 0, 1);
    rmdir(directory);
    return;
  }
  expect("snapshot fixture CPU write", std::fwrite(&legacy, sizeof(legacy), 1, file), 1);
  expect("snapshot fixture workspace write", std::fwrite(&endAddress, sizeof(endAddress), 1, file), 1);
  expect("snapshot fixture watch write", std::fwrite(&legacyWatch, sizeof(legacyWatch), 1, file), 1);
  std::fclose(file);
  reset(0x1111, 0x2222, 0x3333);
  cpuState.iptr = 0x6020;
  cpuState.nWptr = 7;
  uint32_t previousEndAddress = wptrEndAddr;
  uint64_t previousWatch = static_cast<uint64_t>(checkWatch());
  expect("reject legacy snapshot", loadCPUState(directory) == INVALID_CPU_FILE, 1);
  expect("legacy rejection preserves A", cpuState.areg, 0x1111);
  expect("legacy rejection preserves B", cpuState.breg, 0x2222);
  expect("legacy rejection preserves C", cpuState.creg, 0x3333);
  expect("legacy rejection preserves Iptr", cpuState.iptr, 0x6020);
  expect("legacy rejection preserves workspace", static_cast<uint32_t>(cpuState.nWptr), 7);
  expect("legacy rejection preserves trailing address", static_cast<uint32_t>(wptrEndAddr), static_cast<uint32_t>(previousEndAddress));
  expect("legacy rejection preserves watch", checkWatch(), previousWatch);
  allocWptr(2);
  expect("workspace deallocation", static_cast<uint32_t>(cpuState.nWptr), 5);
  expect("snapshot save", saveCPUState(directory) != 0, 0);
  file = std::fopen(filename.c_str(), "rb");
  if (file) {
    std::fseek(file, 0, SEEK_END);
        expect("snapshot appends runtime after legacy prefix", static_cast<uint64_t>(std::ftell(file)) >
          sizeof(legacy) + sizeof(endAddress) + sizeof(legacyWatch), 1);
    std::rewind(file);
    expect("snapshot legacy CPU read", std::fread(&legacy, sizeof(legacy), 1, file), 1);
    expect("snapshot legacy workspace", static_cast<uint32_t>(legacy.nWptr), 5);
    expect("snapshot reserved status", static_cast<uint64_t>(legacy.status), 0);
    std::fclose(file);
  } else {
    expect("snapshot saved file open", 0, 1);
  }
  constexpr uint32_t runtimeWords[] = {0, 33, 39, 169, 170, 180, 192, 212, 229, 550, 550};
  for (uint32_t version = 0; version <= 12; ++version) {
    if (version == 11) continue;
    expect("rewrite current snapshot for version rejection", saveCPUState(directory) != 0, 0);
    file = std::fopen(filename.c_str(), "rb+");
    expect("open version rejection fixture", file != nullptr, 1);
    if (!file) continue;
    std::fseek(file, 0, SEEK_END);
    int64_t runtimeStart = std::ftell(file) - static_cast<int64_t>(cpuRuntimeSnapshotSize());
    std::fseek(file, runtimeStart + static_cast<uint32_t>(sizeof(uint32_t)), SEEK_SET);
    expect("write unsupported snapshot version", std::fwrite(&version, sizeof(version), 1, file), 1);
    std::fflush(file);
    expect("reject unsupported full runtime version", loadCPUState(directory) == INVALID_CPU_FILE, 1);
    if (version < 11) {
      expect("truncate old runtime fixture", ftruncate(fileno(file), runtimeStart +
          static_cast<uint32_t>(runtimeWords[version] * sizeof(uint32_t))) != 0, 0);
      expect("reject old runtime size", loadCPUState(directory) == INVALID_CPU_FILE, 1);
    }
    std::fclose(file);
    expect("version rejection preserves A", cpuState.areg, 0x1111);
    expect("version rejection preserves workspace", static_cast<uint32_t>(cpuState.nWptr), 5);
  }
  expect("rewrite current snapshot for truncation", saveCPUState(directory) != 0, 0);
  file = std::fopen(filename.c_str(), "rb+");
  expect("open current truncation fixture", file != nullptr, 1);
  if (file) {
    std::fseek(file, 0, SEEK_END);
    expect("truncate current runtime by one byte", ftruncate(fileno(file), std::ftell(file) - 1) != 0, 0);
    std::fclose(file);
    expect("reject incomplete current runtime", loadCPUState(directory) == INVALID_CPU_FILE, 1);
  }
  std::remove(filename.c_str());
  rmdir(directory);
}

static void testMemoryReaders() {
  storeBytes(0x20005008U, 4, 0xffffffffU);
  expect("memory initialization succeeds", memoryInit() != 0, 0);
  uint32_t receiveBuffer = undefinedWord;
  expect("ASC2 receive buffer initialized", readBytes(0x20005008U, 4, &receiveBuffer) != 0, 0);
  expect("ASC2 receive buffer reset value", receiveBuffer, 0x00000000U);
  uint32_t address = 0xc000 + BLKSIZE - 2;
  storeBytes(address, 4, 0x44332211);
  uint32_t littleExpected[] = {0x11, 0x2211, 0x332211, 0x44332211};
  uint32_t bigExpected[] = {0x11000000, 0x11220000, 0x11223300, 0x11223344};
  for (int width = 1; width <= 4; ++width) {
    uint32_t little = 0;
    uint32_t big = 0;
    expect("little-endian read success", readBytes(address, width, &little) != 0, 0);
    expect("big-endian read success", readInvBytes(address, width, &big) != 0, 0);
    expect("little-endian read value", little, littleExpected[width - 1]);
    expect("big-endian read value", static_cast<uint32_t>(big), bigExpected[width - 1]);
  }
  uint32_t big = 0;
  expect("big-endian null output", readInvBytes(address, 4, nullptr) == READ_UNUSED_MEM, 1);
  expect("big-endian oversized read", readInvBytes(address, 5, &big) == READ_TOO_LARGE, 1);
  expect("big-endian oversized sentinel", static_cast<uint32_t>(big), undefinedWord);
  expect("big-endian partial missing read", readInvBytes(address + 2, 4, &big) == READ_UNUSED_MEM, 1);
  expect("big-endian missing sentinel", static_cast<uint32_t>(big), undefinedWord);

  uint32_t wrapped = 0;
  expect("word write wraps guest address space", storeBytes(WordMask, BytesPerWord, 0x88776655) != 0, 0);
  expect("word read wraps guest address space", readBytes(WordMask, BytesPerWord, &wrapped) != 0, 0);
  expect("wrapped word preserves all bits", wrapped, 0x88776655);

  char filename[] = "/tmp/st20-oversized-XXXXXX";
  int file = mkstemp(filename);
  expect("oversized firmware fixture opens", file >= 0, 1);
  if (file >= 0) {
    expect("oversized firmware fixture length", ftruncate(file, static_cast<off_t>(UINT32_MAX) + 1) != 0, 0);
    close(file);
    uint32_t length = 123;
    expect("firmware larger than word length is rejected", bulkLoadBytes(0, filename, nullptr, &length) == INVALID_BYTE_FILE, 1);
    expect("oversized firmware leaves byte count unchanged", length, 123);
    unlink(filename);
  }
}

static void testRuntimeDefaults() {
  PARMS parms{1, {TIMER_GUESS_CH}, {"0x12345678"}};
  st20Init(&parms);
  expect("configured high priority clock", static_cast<uint32_t>(omrState.ClockRegHP), 0x12345678);
  expect("configured low priority clock", static_cast<uint32_t>(omrState.ClockRegLP), 0x12345678);
  undefinedWord = 0xa5a5a5a5;
  reset(32, 1, 2);
  shl_(0);
  expect("configured undefined shift", cpuState.areg, undefinedWord);
  expect("configured undefined stack", cpuState.creg, undefinedWord);
  uint32_t value = 0;
  expect("configured undefined read error", readBytes(0x70002000, 4, &value) != 0, 1);
  expect("configured undefined read value", value, undefinedWord);
  allocBytes(0xa000, 4);
  readBytes(0xa000, 4, &value);
  expect("configured undefined allocation", value, undefinedWord);
  undefinedWord = UNDEFINED_WORD_OLD;
}

static uint32_t observedAddress = 0;
static int observedAccesses = 0;

static void testDeviceAccess() {
  setDeviceAccessObserver(+[](uint32_t address) {
    observedAddress = address;
    ++observedAccesses;
  });
  int (*loads[])(uint32_t) = {devlb_, devls_, devlw_};
  int (*stores[])(uint32_t) = {devsb_, devss_, devsw_};
  uint32_t masks[] = {0xff, 0xffff, 0xffffffff};
  for (int index = 0; index < 3; ++index) {
    uint32_t address = 0xb000U + static_cast<uint32_t>(index) * 4U;
    reset(address, 0x89abcdef, 0x12345678);
    expect("device store succeeds", stores[index](0) != 0, 0);
    expect("device store stack", cpuState.areg, 0x12345678);
    uint32_t value = 0;
    readBytes(address, 1 << index, &value);
    expect("device store width", value, 0x89abcdefU & masks[index]);
    reset(address, 0x12345678, 0x87654321);
    expect("device load succeeds", loads[index](0) != 0, 0);
    expect("device load width", cpuState.areg, 0x89abcdefU & masks[index]);
    expect("device load preserves B", cpuState.breg, 0x12345678);
    expect("device load preserves C", cpuState.creg, 0x87654321);
    expect("device observer address", observedAddress, address);
  }
  expect("device observer count", static_cast<uint32_t>(observedAccesses), 6);
  setDeviceAccessObserver(nullptr);
  reset(0xb000, 0, 0);
  devlb_(0);
  expect("disabled device observer", static_cast<uint32_t>(observedAccesses), 6);
  reset(0x70003000, 1, 2);
  expect("unused device memory remains tolerated", devlw_(0) != 0, 0);
  expect("unused device memory value", cpuState.areg, undefinedWord);
}

static void testDeviceMemory() {
  struct DeviceState {
    uint32_t bits{0};
    uint32_t lastAddress{0};
    uint32_t lastValue{0};
    int lastWidth{0};
    int reads{0};
    int writes{0};
    int error{0};
    bool checkOrder{false};
    uint32_t expectedRam{0};
  } state;
  MemoryDevice device{0x20010000, 0x2001000f, &state,
      +[](void *context, uint32_t address, int width, uint32_t *value) {
        auto &deviceState = *static_cast<DeviceState *>(context);
        ++deviceState.reads;
        deviceState.lastAddress = address;
        deviceState.lastWidth = width;
        if (deviceState.checkOrder) {
          uint32_t ram = 0;
          expect("MMIO read observes preceding RAM store", readBytes(0xb100, 4, &ram) != 0, 0);
          expect("MMIO read RAM sequence", ram, deviceState.expectedRam);
        }
        if (deviceState.error) return deviceState.error;
        *value = deviceState.bits;
        deviceState.bits = 0;
        return 0;
      },
      +[](void *context, uint32_t address, int width, uint32_t value) {
        auto &deviceState = *static_cast<DeviceState *>(context);
        ++deviceState.writes;
        deviceState.lastAddress = address;
        deviceState.lastWidth = width;
        deviceState.lastValue = value;
        if (deviceState.error) return deviceState.error;
        deviceState.bits &= ~value;
        if (deviceState.checkOrder) storeBytes(0xb104, 4, deviceState.bits);
        return 0;
      }};
  expect("seed MMIO backing RAM", storeBytes(device.start, 4, 0xfeedbeef) != 0, 0);
  expect("register MMIO device", registerMemoryDevice(device) != 0, 0);
  expect("reject duplicate MMIO region", registerMemoryDevice(device) == INVALID_MEMORY_DEVICE, 1);
  MemoryDevice overlapping = device;
  overlapping.start = device.end;
  overlapping.end += 4;
  expect("reject overlapping MMIO region", registerMemoryDevice(overlapping) == INVALID_MEMORY_DEVICE, 1);
  MemoryDevice invalid = device;
  invalid.start = device.end + 1;
  expect("reject reversed MMIO bounds", registerMemoryDevice(invalid) == INVALID_MEMORY_DEVICE, 1);
  invalid = {0x20011000, 0x20011003, nullptr, nullptr, nullptr};
  expect("reject MMIO without access handlers", registerMemoryDevice(invalid) == INVALID_MEMORY_DEVICE, 1);
  int (*loads[])(uint32_t) = {devlb_, devls_, devlw_};
  int (*stores[])(uint32_t) = {devsb_, devss_, devsw_};
  const uint32_t loadBytes[] = {0xf02f, 0xf22f, 0xf42f};
  const uint32_t storeOpcodes[] = {0xf12f, 0xf32f, 0xf52f};
  const uint32_t masks[] = {0xff, 0xffff, 0xffffffff};
  for (int index = 0; index < 3; ++index) {
    int width = 1 << index;
    uint32_t mask = masks[index];
    state.bits = 0x89abcdef;
    int previousReads = state.reads;
    reset(device.start, 0x12345678, 0x87654321);
    cpuState.running = true;
    cpuState.iptr = 0x7a00;
    storeBytes(0x7a00, 2, loadBytes[index]);
    expect("MMIO device load decode", decodeNextInstr() != 0, 0);
    expect("MMIO device load handler", instrEntry[instrCode].function == loads[index], 1);
    int watchFlag = 0;
    expect("MMIO device load executes", execInstr(&watchFlag) != 0, 0);
    expect("MMIO load zero extends", cpuState.areg, 0x89abcdefU & mask);
    expect("MMIO load preserves B", cpuState.breg, 0x12345678);
    expect("MMIO load preserves C", cpuState.creg, 0x87654321);
    expect("MMIO load is one transaction", static_cast<uint32_t>(state.reads - previousReads), 1);
    expect("MMIO load exact width", static_cast<uint32_t>(state.lastWidth), static_cast<uint32_t>(width));
    expect("MMIO load exact address", state.lastAddress, device.start);
    expect("MMIO read-clear side effect", state.bits, 0);

    state.bits = 0xffffffff;
    int previousWrites = state.writes;
    reset(device.start, 0x89abcdef, 0x12345678);
    cpuState.iptr = 0x7a00;
    storeBytes(0x7a00, 2, storeOpcodes[index]);
    expect("MMIO device store decode", decodeNextInstr() != 0, 0);
    expect("MMIO device store handler", instrEntry[instrCode].function == stores[index], 1);
    expect("MMIO device store executes", execInstr(&watchFlag) != 0, 0);
    expect("MMIO store masks value", state.lastValue, 0x89abcdefU & mask);
    expect("MMIO write-one-clear side effect", state.bits, ~(0x89abcdefU & mask));
    expect("MMIO store is one transaction", static_cast<uint32_t>(state.writes - previousWrites), 1);
    expect("MMIO store exact width", static_cast<uint32_t>(state.lastWidth), static_cast<uint32_t>(width));
    expect("MMIO store A stack", cpuState.areg, 0x12345678);
    expect("MMIO store B undefined", cpuState.breg, undefinedWord);
    expect("MMIO store C undefined", cpuState.creg, undefinedWord);
  }
  state.checkOrder = true;
  state.expectedRam = 0x76543210;
  storeBytes(0xb100, 4, state.expectedRam);
  state.bits = 0xa5a55a5a;
  reset(device.start, 1, 2);
  expect("ordered device read", devlw_(0) != 0, 0);
  expect("ordered device read result", cpuState.areg, 0xa5a55a5a);
  state.bits = 0xffffffff;
  reset(device.start, 0xffff0000, 2);
  expect("ordered device write", devsw_(0) != 0, 0);
  uint32_t value = 0;
  readBytes(0xb104, 4, &value);
  expect("following RAM load sees completed device write", value, 0xffff);
  state.checkOrder = false;
  state.bits = 0x12345678;
  reset(device.start, 1, 2);
  expect("normal load shares MMIO bus", ldnl_(0) != 0, 0);
  expect("normal load sees device value", cpuState.areg, 0x12345678);
  expect("normal load performs read-clear", state.bits, 0);
  state.bits = 0xffffffff;
  reset(device.start, 0x00ff00ff, 2);
  expect("normal store shares MMIO bus", stnl_(0) != 0, 0);
  expect("normal store performs write-one-clear", state.bits, 0xff00ff00);
  int previousReads = state.reads;
  int previousWrites = state.writes;
  for (uint32_t address : {device.start - 1, device.end - 1}) {
    expect("reject boundary-crossing MMIO read", readBytes(address, 4, &value) == INVALID_DEVICE_ACCESS, 1);
    expect("reject boundary-crossing MMIO write", storeBytes(address, 4, 0) == INVALID_DEVICE_ACCESS, 1);
  }
  expect("invalid MMIO read has no side effect", static_cast<uint32_t>(state.reads), static_cast<uint32_t>(previousReads));
  expect("invalid MMIO write has no side effect", static_cast<uint32_t>(state.writes), static_cast<uint32_t>(previousWrites));
  MemoryDevice adjacent = device;
  adjacent.start = device.end + 1;
  adjacent.end = device.end + 4;
  expect("register adjacent MMIO region", registerMemoryDevice(adjacent) != 0, 0);
  expect("reject read straddling adjacent devices", readBytes(device.end - 1, 4, &value) == INVALID_DEVICE_ACCESS, 1);
  expect("reject write straddling adjacent devices", storeBytes(device.end - 1, 4, 0) == INVALID_DEVICE_ACCESS, 1);
  expect("unregister adjacent MMIO region", unregisterMemoryDevice(adjacent.start) != 0, 0);
  MemoryDevice highEdge = device;
  highEdge.start = 0xfffffffc;
  highEdge.end = 0xffffffff;
  MemoryDevice lowEdge = device;
  lowEdge.start = 0;
  lowEdge.end = 3;
  expect("register high-edge MMIO region", registerMemoryDevice(highEdge) != 0, 0);
  expect("register low-edge MMIO region", registerMemoryDevice(lowEdge) != 0, 0);
  expect("reject wrapped MMIO read", readBytes(0xfffffffeU, 4, &value) == INVALID_DEVICE_ACCESS, 1);
  expect("reject wrapped MMIO write", storeBytes(0xfffffffeU, 4, 0) == INVALID_DEVICE_ACCESS, 1);
  expect("rejected boundary reads invoke no callback", static_cast<uint32_t>(state.reads), static_cast<uint32_t>(previousReads));
  expect("rejected boundary writes invoke no callback", static_cast<uint32_t>(state.writes), static_cast<uint32_t>(previousWrites));
  state.bits = 0xfedcba98;
  expect("last-address byte MMIO read", readBytes(0xffffffffU, 1, &value) != 0, 0);
  expect("last-address MMIO byte mask", value, 0x98);
  expect("first-address MMIO word write", storeBytes(0, 4, 0x12345678) != 0, 0);
  expect("first-address MMIO word value", state.lastValue, 0x12345678);
  expect("unregister high-edge MMIO region", unregisterMemoryDevice(highEdge.start) != 0, 0);
  expect("reject wrap into low-edge MMIO read", readBytes(0xfffffffeU, 4, &value) == INVALID_DEVICE_ACCESS, 1);
  expect("reject wrap into low-edge MMIO write", storeBytes(0xfffffffeU, 4, 0) == INVALID_DEVICE_ACCESS, 1);
  expect("unregister low-edge MMIO region", unregisterMemoryDevice(lowEdge.start) != 0, 0);
  state.error = READ_UNUSED_MEM;
  reset(device.start, 1, 2);
  expect("device read error is not RAM fallback", devlw_(0) == READ_UNUSED_MEM, 1);
  expect("device failed read retains undefined result", cpuState.areg, undefinedWord);
  expect("device failed read preserves B", cpuState.breg, 1);
  expect("device failed read preserves C", cpuState.creg, 2);
  state.error = WRITE_FAILURE;
  reset(device.start, 0xff, 2);
  expect("device write propagates error", devsb_(0) == WRITE_FAILURE, 1);
  state.error = 0;
  MemoryDevice readOnly = device;
  readOnly.start = 0x20011000;
  readOnly.end = 0x20011003;
  readOnly.write = nullptr;
  expect("register read-only device", registerMemoryDevice(readOnly) != 0, 0);
  expect("read-only device rejects store", storeBytes(readOnly.start, 4, 1) == DEVICE_ACCESS_UNSUPPORTED, 1);
  expect("unregister read-only device", unregisterMemoryDevice(readOnly.start) != 0, 0);
  MemoryDevice writeOnly = readOnly;
  writeOnly.read = nullptr;
  writeOnly.write = device.write;
  expect("register write-only device", registerMemoryDevice(writeOnly) != 0, 0);
  expect("write-only device rejects load", readBytes(writeOnly.start, 4, &value) == DEVICE_ACCESS_UNSUPPORTED, 1);
  expect("unregister write-only device", unregisterMemoryDevice(writeOnly.start) != 0, 0);
  expect("unregister MMIO device", unregisterMemoryDevice(device.start) != 0, 0);
  expect("reject unregister missing device", unregisterMemoryDevice(device.start) == INVALID_MEMORY_DEVICE, 1);
  expect("mapped accesses did not mutate backing RAM", readBytes(device.start, 4, &value) != 0, 0);
  expect("mapped MMIO backing RAM remains intact", value, 0xfeedbeef);
  expect("unregistered MMIO restores RAM writes", storeBytes(device.start, 4, 0x76543210) != 0, 0);
  expect("unregistered MMIO restores RAM reads", readBytes(device.start, 4, &value) != 0, 0);
  expect("unregistered MMIO RAM value", value, 0x76543210);
}

static void testDeviceMove() {
  for (uint32_t sourceOffset = 0; sourceOffset < 4; ++sourceOffset) {
    for (uint32_t destinationOffset = 0; destinationOffset < 4; ++destinationOffset) {
      for (uint32_t length = 0; length <= 17; ++length) {
        for (uint32_t index = 0; index < 32; ++index) {
          storeBytes(0x1a0000 + index, 1, static_cast<uint32_t>((index * 7U + 0x20U) & 255U));
          storeBytes(0x1a1000 + index, 1, 0xa5);
        }
        reset(length, 0x1a1000 + destinationOffset, 0x1a0000 + sourceOffset);
        expect("devmove RAM copy succeeds", devmove_(0) != 0, 0);
        expect("devmove A undefined", cpuState.areg, undefinedWord);
        expect("devmove B undefined", cpuState.breg, undefinedWord);
        expect("devmove C undefined", cpuState.creg, undefinedWord);
        for (uint32_t index = 0; index < 32; ++index) {
          uint32_t value = 0;
          readBytes(0x1a1000 + index, 1, &value);
          uint32_t expected = index >= destinationOffset && index < destinationOffset + length
              ? ((sourceOffset + index - destinationOffset) * 7U + 0x20U) & 255U : 0xa5U;
          expect("devmove exact RAM extent", value, expected);
        }
      }
    }
  }
  struct Event { uint32_t address; int width; bool write; };
  struct BusState {
    unsigned char source[64]{};
    unsigned char destination[64]{};
    std::vector<Event> events;
    int reads{0};
    int writes{0};
    int failRead{-1};
    int failWrite{-1};
    int peripheralTouches{0};
  } state;
  MemoryDevice sourceDevice{0x40010000, 0x4001003f, &state,
      +[](void *context, uint32_t address, int width, uint32_t *value) {
        auto &bus = *static_cast<BusState *>(context);
        bus.events.push_back({address, width, false});
        if (bus.reads++ == bus.failRead) return READ_UNUSED_MEM;
        *value = 0;
        for (int index = 0; index < width; ++index)
          *value |= static_cast<uint32_t>(bus.source[address - 0x40010000U + static_cast<uint32_t>(index)])
                    << (static_cast<unsigned int>(index) * 8U);
        return 0;
      }, nullptr};
  MemoryDevice destinationDevice{0x40020000, 0x4002003f, &state, nullptr,
      +[](void *context, uint32_t address, int width, uint32_t value) {
        auto &bus = *static_cast<BusState *>(context);
        bus.events.push_back({address, width, true});
        if (bus.writes++ == bus.failWrite) return WRITE_FAILURE;
        for (int index = 0; index < width; ++index)
          bus.destination[address - 0x40020000U + static_cast<uint32_t>(index)] =
              static_cast<unsigned char>(value >> (static_cast<unsigned int>(index) * 8U));
        return 0;
      }};
  expect("register devmove source device", registerMemoryDevice(sourceDevice) != 0, 0);
  expect("register devmove destination device", registerMemoryDevice(destinationDevice) != 0, 0);
  auto prepare = [&state] {
    state.events.clear();
    state.reads = state.writes = 0;
    state.failRead = state.failWrite = -1;
    for (uint32_t index = 0; index < 64; ++index) {
      state.source[index] = static_cast<unsigned char>(index * 7U + 0x20U);
      state.destination[index] = 0xa5;
    }
  };
  for (uint32_t length : {0x7fffffffU, 0x80000000U, 0xffffffffU}) {
    prepare();
    state.failRead = 0;
    reset(length, destinationDevice.start, sourceDevice.start);
    expect("move honors unsigned full-width count", move_(0) != 0, 1);
    expect("large move attempts first source read", static_cast<uint32_t>(state.reads), 1);
    expect("failed large move never writes", static_cast<uint32_t>(state.writes), 0);
    expect("failed large move A undefined", cpuState.areg, undefinedWord);
    expect("failed large move B undefined", cpuState.breg, undefinedWord);
    expect("failed large move C undefined", cpuState.creg, undefinedWord);
  }
  prepare();
  state.failRead = 0;
  reset(0, destinationDevice.start, sourceDevice.start);
  expect("zero move ignores failing source", move_(0) != 0, 0);
  expect("zero move performs no bus transaction", state.events.size(), 0);
  for (uint32_t sourceOffset = 0; sourceOffset < 4; ++sourceOffset) {
    for (uint32_t destinationOffset = 0; destinationOffset < 4; ++destinationOffset) {
      for (uint32_t length = 0; length <= 17; ++length) {
        prepare();
        reset(length, destinationDevice.start + destinationOffset, sourceDevice.start + sourceOffset);
        expect("move MMIO copy succeeds", move_(0) != 0, 0);
        uint32_t prefix = std::min(length, (4U - sourceOffset) % 4U);
        uint32_t remainder = length - prefix;
        uint32_t transactions = sourceOffset == destinationOffset
            ? prefix + remainder / 4U + remainder % 4U : length;
        expect("move aligned word transaction count", state.events.size(), transactions * 2U);
        uint32_t copied = 0;
        for (std::size_t event = 0; event + 1 < state.events.size(); event += 2) {
          const auto &read = state.events[event];
          const auto &write = state.events[event + 1];
          expect("move exact source cursor", read.address, sourceDevice.start + sourceOffset + copied);
          expect("move exact destination cursor", write.address, destinationDevice.start + destinationOffset + copied);
          expect("move reads before writes", read.write, 0);
          expect("move paired write", write.write, 1);
          expect("move matched bus widths", static_cast<uint32_t>(read.width), static_cast<uint32_t>(write.width));
          expect("move never touches beyond requested span", copied + static_cast<uint32_t>(read.width) <= length, 1);
          copied += static_cast<uint32_t>(read.width);
        }
        for (uint32_t index = 0; index < 64; ++index) {
          uint32_t expected = index >= destinationOffset && index < destinationOffset + length
              ? state.source[sourceOffset + index - destinationOffset] : 0xa5U;
          expect("move exact MMIO copied extent", state.destination[index], expected);
        }
      }
    }
  }
  for (bool readFailure : {true, false}) {
    prepare();
    if (readFailure) state.failRead = 1;
    else state.failWrite = 1;
    reset(12, destinationDevice.start, sourceDevice.start);
    expect("move reports bus failure", move_(0) != 0, 1);
    expect("move stops at failing transaction", state.events.size(), readFailure ? 3U : 4U);
    for (uint32_t index = 0; index < 12; ++index)
      expect("move retains completed words only", state.destination[index], index < 4 ? state.source[index] : 0xa5U);
  }
  prepare();
  reset(2, 20, 20);
  move2dinit_(0);
  reset(13, destinationDevice.start, sourceDevice.start);
  expect("move2dall word bus copy succeeds", move2dall_(0) != 0, 0);
  expect("move2dall exact row transaction count", state.events.size(), 16);
  for (uint32_t index = 0; index < 64; ++index)
    expect("move2dall preserves stride gaps", state.destination[index],
        index < 13 || (index >= 20 && index < 33) ? state.source[index] : 0xa5U);
  for (uint32_t sourceOffset = 0; sourceOffset < 4; ++sourceOffset) {
    for (uint32_t destinationOffset = 0; destinationOffset < 4; ++destinationOffset) {
      for (uint32_t length = 0; length <= 17; ++length) {
        prepare();
        reset(length, destinationDevice.start + destinationOffset, sourceDevice.start + sourceOffset);
        expect("devmove MMIO copy succeeds", devmove_(0) != 0, 0);
        std::size_t transactions = (length + 3U) / 4U;
        expect("devmove minimum source reads", static_cast<uint32_t>(state.reads), transactions);
        expect("devmove minimum destination writes", static_cast<uint32_t>(state.writes), transactions);
        expect("devmove no extra bus accesses", state.events.size(), transactions * 2);
        if (state.events.size() == transactions * 2) {
          for (std::size_t chunk = 0; chunk < transactions; ++chunk) {
            uint32_t offset = static_cast<uint32_t>(chunk) * 4U;
            uint32_t remaining = length - offset;
            uint32_t width = remaining < 4 ? remaining : 4;
            expect("devmove increasing source address", state.events[chunk * 2].address, sourceDevice.start + sourceOffset + offset);
            expect("devmove increasing destination address", state.events[chunk * 2 + 1].address, destinationDevice.start + destinationOffset + offset);
            expect("devmove exact read width", static_cast<uint32_t>(state.events[chunk * 2].width), width);
            expect("devmove exact write width", static_cast<uint32_t>(state.events[chunk * 2 + 1].width), width);
            expect("devmove reads before corresponding write", state.events[chunk * 2].write, 0);
            expect("devmove corresponding write follows read", state.events[chunk * 2 + 1].write, 1);
          }
        }
        for (uint32_t index = 0; index < 64; ++index) {
          uint32_t expected = index >= destinationOffset && index < destinationOffset + length
              ? state.source[sourceOffset + index - destinationOffset] : 0xa5U;
          expect("devmove MMIO exact copied extent", state.destination[index], expected);
        }
      }
    }
  }
  prepare();
  reset(7, destinationDevice.start, sourceDevice.start);
  cpuState.running = true;
  cpuState.iptr = 0xa000;
  storeBytes(0xa000, 2, 0xf462);
  expect("encoded devmove decode", decodeNextInstr() != 0, 0);
  int watchFlag = 0;
  expect("encoded devmove executes", execInstr(&watchFlag) != 0, 0);
  expect("encoded devmove next Iptr", cpuState.iptr, 0xa002);
  expect("encoded devmove transaction count", state.events.size(), 4);
  for (bool readFailure : {true, false}) {
    prepare();
    if (readFailure) state.failRead = 1;
    else state.failWrite = 1;
    reset(12, destinationDevice.start, sourceDevice.start);
    expect("devmove propagates bus failure", devmove_(0) == (readFailure ? READ_UNUSED_MEM : WRITE_FAILURE), 1);
    expect("devmove stops after bus failure", state.events.size(), readFailure ? 3U : 4U);
    for (uint32_t index = 0; index < 12; ++index)
      expect("devmove keeps completed chunks only", state.destination[index], index < 4 ? state.source[index] : 0xa5U);
    expect("failed devmove stack remains undefined", cpuState.areg, undefinedWord);
  }
  prepare();
  MemoryDevice peripheral{0x20030000, 0x2003003f, &state,
      +[](void *context, uint32_t, int, uint32_t *) {
        ++static_cast<BusState *>(context)->peripheralTouches;
        return READ_UNUSED_MEM;
      },
      +[](void *context, uint32_t, int, uint32_t) {
        ++static_cast<BusState *>(context)->peripheralTouches;
        return WRITE_FAILURE;
      }};
  expect("register excluded peripheral", registerMemoryDevice(peripheral) != 0, 0);
  reset(4, destinationDevice.start, peripheral.start);
  expect("devmove rejects peripheral source", devmove_(0) == DEVICE_MOVE_PERIPHERAL_UNSUPPORTED, 1);
  reset(4, peripheral.start, sourceDevice.start);
  expect("devmove rejects peripheral destination", devmove_(0) == DEVICE_MOVE_PERIPHERAL_UNSUPPORTED, 1);
  reset(4, destinationDevice.start, 0x1ffffffe);
  expect("devmove rejects span entering peripherals", devmove_(0) == DEVICE_MOVE_PERIPHERAL_UNSUPPORTED, 1);
  reset(0, peripheral.start, peripheral.start);
  expect("zero devmove ignores peripheral addresses", devmove_(0) != 0, 0);
  expect("excluded peripherals have no side effects", static_cast<uint32_t>(state.peripheralTouches), 0);
  expect("rejected copies do not access MMIO", state.events.size(), 0);
  reset(8, sourceDevice.start + 1, sourceDevice.start);
  expect("devmove rejects overlapping ranges", devmove_(0) == INVALID_DEVICE_MOVE_RANGE, 1);
  reset(16, destinationDevice.start, 0xfffffff8U);
  expect("devmove rejects wrapping source", devmove_(0) == INVALID_DEVICE_MOVE_RANGE, 1);
  reset(16, 0xfffffff8U, sourceDevice.start);
  expect("devmove rejects wrapping destination", devmove_(0) == INVALID_DEVICE_MOVE_RANGE, 1);
  reset(0x80000000, destinationDevice.start, 0xc0000000);
  expect("devmove validates unsigned high-bit count", devmove_(0) == INVALID_DEVICE_MOVE_RANGE, 1);
  expect("invalid ranges have no bus transactions", state.events.size(), 0);
  expect("unregister devmove source", unregisterMemoryDevice(sourceDevice.start) != 0, 0);
  expect("unregister devmove destination", unregisterMemoryDevice(destinationDevice.start) != 0, 0);
  expect("unregister excluded peripheral", unregisterMemoryDevice(peripheral.start) != 0, 0);
  storeBytes(0xffffffffU, 1, 0x66);
  reset(1, 0x1a2000, 0xffffffffU);
  expect("devmove can end at last address", devmove_(0) != 0, 0);
  uint32_t value = 0;
  readBytes(0x1a2000, 1, &value);
  expect("devmove last-address byte", value, 0x66);
}

static void testTimerClocks() {
  omrState = {};
  omrState.ClockEnables = HPTIMER_MASK | LPTIMER_MASK;
  advanceTimers(60);
  expect("HP fractional cycle boundary", omrState.ClockRegHP, 0);
  advanceTimers(1);
  expect("HP first complete microsecond", omrState.ClockRegHP, 1);
  advanceTimers(CPU_CLOCK - 61);
  expect("HP ticks in one second", omrState.ClockRegHP, HP_TIMER_HZ);
  expect("LP ticks in one second", omrState.ClockRegLP, LP_TIMER_HZ);
  expect("HP exact-second phase", omrState.ClockPhaseHP, 0);
  expect("LP exact-second phase", omrState.ClockPhaseLP, 0);
  omrState = {};
  omrState.ClockEnables = 3;
  for (int instruction = 0; instruction < 32400; ++instruction) advanceTimers(39);
  expect("HP retains instruction overshoot", omrState.ClockRegHP, 20800);
  expect("LP retains instruction overshoot", omrState.ClockRegLP, 325);
  omrState = {};
  omrState.ClockEnables = 3;
  advanceTimers(125);
  expect("multiple HP ticks per instruction", omrState.ClockRegHP, 2);
  omrState.ClockRegHP = 0xffffffff;
  omrState.ClockRegLP = 0xffffffff;
  omrState.ClockPhaseHP = 0;
  omrState.ClockPhaseLP = 0;
  advanceTimers(CPU_CLOCK);
  expect("HP clock wraps", omrState.ClockRegHP, HP_TIMER_HZ - 1);
  expect("LP clock wraps", omrState.ClockRegLP, LP_TIMER_HZ - 1);
  omrState.ClockEnables = 0;
  advanceTimers(17);
  expect("disabled HP clock frozen", omrState.ClockRegHP, HP_TIMER_HZ - 1);
  expect("disabled LP clock frozen", omrState.ClockRegLP, LP_TIMER_HZ - 1);
  expect("disabled HP phase frozen", omrState.ClockPhaseHP, 0);
  expect("disabled LP phase frozen", omrState.ClockPhaseLP, 0);
  omrState.ClockPhaseHP = 123;
  omrState.ClockPhaseLP = 456;
  initTimer();
  expect("timer initialization resets HP phase", omrState.ClockPhaseHP, 0);
  expect("timer initialization resets LP phase", omrState.ClockPhaseLP, 0);
  for (uint32_t priority : {0U, 1U}) {
    cpuState.priority = static_cast<unsigned char>(priority);
    for (int workspace : {6, 7}) {
      reset(0x1234, 0x5678, 0x9abc);
      omrState.ClockRegHP = 10;
      omrState.ClockRegLP = 20;
      cpuState.nWptr = workspace;
      ldtimer_(0);
      expect("ldtimer uses process priority", cpuState.areg,
             priority == HIGH_PRIORITY ? 10 : 20);
      ldpri_(0);
      expect("ldpri uses process priority", cpuState.areg, priority);
    }
  }
}

static void testRealWorkspace() {
  initWatch();
  struct Adjustment {
    uint32_t value;
    uint32_t workspace;
    uint32_t words;
  };
  const Adjustment adjustments[] = {
      {0, 0xfffffffc, 7},
      {1, 0, 6},
      {0xffffffff, 0xfffffff8, 8},
      {0x80000000, 0xfffffffc, MAX_WPTR},
      {0x7fffffff, 0xfffffff8, 0},
  };
  for (const auto &entry : adjustments) {
    reset(0x12345678, 0x87654321, WordMask);
    cpuState.wptr = 0xfffffffc;
    cpuState.nWptr = 7;
    expect("word workspace adjustment succeeds", ajw_(entry.value) != 0, 0);
    expect("word workspace adjustment wraps", cpuState.wptr, entry.workspace);
    expect("word workspace adjustment interprets signed count", static_cast<uint32_t>(cpuState.nWptr), entry.words);
    expect("word workspace adjustment preserves A", cpuState.areg, 0x12345678);
    expect("word workspace adjustment preserves B", cpuState.breg, 0x87654321);
    expect("word workspace adjustment preserves C", cpuState.creg, WordMask);
  }
  cpuState.nWptr = 1;
  setWptr(0x10000);
  uint32_t address = 0;
  expect("workspace address outside old allocation", addrWptrWord(100, &address) != 0, 0);
  expect("workspace address arithmetic", static_cast<uint32_t>(address), 0x10190);
  storeWptrWord(100, 0x12345678);
  uint32_t value = 0;
  expect("workspace load outside old allocation", readWptrWord(100, &value) != 0, 0);
  expect("workspace memory value", static_cast<uint32_t>(value), 0x12345678);
  allocWptr(0x10000);
  expect("ajw not restricted by allocation count", cpuState.wptr, 0x50000);
  allocWptr(static_cast<uint32_t>(-0x10000));
  expect("ajw reverses pointer adjustment", cpuState.wptr, 0x10000);
  setWptr(0xfffffffc);
  allocWptr(1);
  expect("workspace pointer wraps", cpuState.wptr, 0);
  setWptr(0x10000);
  storeBytes(0x11000, 4, 0x87654321);
  reset(0x11000, 1, 2);
  cpuState.nWptr = 7;
  expect("gajw succeeds", gajw_(0) != 0, 0);
  expect("gajw new pointer", cpuState.wptr, 0x11000);
  expect("gajw returns old pointer", cpuState.areg, 0x10000);
  expect("gajw preserves B", cpuState.breg, 1);
  expect("gajw preserves C", cpuState.creg, 2);
  expect("gajw preserves workspace display count", static_cast<uint32_t>(cpuState.nWptr), 7);
  uint32_t stored = 0;
  readBytes(0x11000, 4, &stored);
  expect("gajw does not overwrite workspace", stored, 0x87654321);
  expect("gajw workspace offset beyond display count", addrWptrWord(100, &address) != 0, 0);
  expect("gajw workspace offset address", static_cast<uint32_t>(address), 0x11190);
  expect("gajw negative workspace offset", addrWptrWord(WordMask, &address) != 0, 0);
  expect("gajw negative workspace address", static_cast<uint32_t>(address), 0x10ffc);
  expect("gajw exchange back succeeds", gajw_(0) != 0, 0);
  expect("gajw exchange back pointer", cpuState.wptr, 0x10000);
  expect("gajw exchange back A", cpuState.areg, 0x11000);

  for (uint32_t workspace : {0U, 0x80000000U, 0xfffffffcU}) {
    reset(workspace, 0x12345678, 0x87654321);
    cpuState.iptr = 0x6020;
    storeBytes(0x6020, 2, 0xfc23);
    expect("gajw decode succeeds", decodeNextInstr() != 0, 0);
    int breakFlag = 0;
    expect("gajw encoded execution succeeds", execInstr(&breakFlag) != 0, 0);
    expect("gajw encoded new pointer", cpuState.wptr, workspace);
    expect("gajw encoded old pointer", cpuState.areg, 0x10000);
    expect("gajw encoded preserves B", cpuState.breg, 0x12345678);
    expect("gajw encoded preserves C", cpuState.creg, 0x87654321);
    expect("gajw encoded next instruction", cpuState.iptr, 0x6022);
    expect("gajw encoded preserves display count", static_cast<uint32_t>(cpuState.nWptr), 7);
    setWptr(0x10000);
  }

  reset(0, 0x1234, 0x5678);
  cpuState.nWptr = 0;
  cpuState.iptr = 0x6040;
  storeBytes(0x6040, 2, 0xb060);
  expect("negative ajw operand decodes", decodeNextInstr() != 0, 0);
  expect("nfix produces a 32-bit operand", operand, 0xfffffff0);
  int breakFlag = 0;
  expect("negative ajw operand executes", execInstr(&breakFlag) != 0, 0);
  expect("negative ajw adjusts workspace", cpuState.wptr, 0xffc0);
  expect("negative ajw allocates display words", static_cast<uint32_t>(cpuState.nWptr), 16);
  expect("positive ajw operand executes", ajw_(16) != 0, 0);
  expect("positive ajw restores workspace", cpuState.wptr, 0x10000);
  expect("positive ajw releases display words", static_cast<uint32_t>(cpuState.nWptr), 0);

  storeBytes(0x6042, 2, 0x8f60);
  expect("negative adc operand decodes", decodeNextInstr() != 0, 0);
  expect("negative adc operand preserves all bits", operand, 0xffffffff);
  expect("negative adc operand executes", execInstr(&breakFlag) != 0, 0);
  expect("negative adc operand uses signed arithmetic", cpuState.areg, 0xffffffff);
  cpuState.nWptr = 7;
}

static void schedulerFixture(uint32_t workspace, unsigned char priority = LOW_PRIORITY) {
  omrState = {};
  resetScheduler();
  cpuState.priority = priority;
  cpuState.wptr = workspace;
  cpuState.iptr = 0x3000;
  omrState.ClockEnables = 3;
  omrState.Enables = 0xffffc000;
  storeBytes(0x3000, 2, 0xf063);
}

static void testReadyQueueControls() {
  for (unsigned char priority : {static_cast<unsigned char>(HIGH_PRIORITY), static_cast<unsigned char>(LOW_PRIORITY)}) {
    schedulerFixture(0x150000, HIGH_PRIORITY);
    int (*storeFront)(uint32_t) = priority == HIGH_PRIORITY ? sthf_ : stlf_;
    int (*storeBack)(uint32_t) = priority == HIGH_PRIORITY ? sthb_ : stlb_;
    int (*saveQueue)(uint32_t) = priority == HIGH_PRIORITY ? saveh_ : savel_;
    for (uint32_t pointer : {0x151000U, NO_PROCESS}) {
      uint32_t oldBack = omrState.ProcQueueBPtr[priority];
      cpuState.areg = pointer;
      cpuState.breg = 0x1234;
      cpuState.creg = 0x5678;
      expect("store queue front succeeds", storeFront(0) != 0, 0);
      expect("store queue front value", omrState.ProcQueueFPtr[priority], pointer);
      expect("store queue front preserves back", omrState.ProcQueueBPtr[priority], oldBack);
      expect("store queue front pops A", cpuState.areg, 0x1234);
      expect("store queue front shifts C into B", cpuState.breg, 0x5678);
      expect("store queue front leaves C undefined", cpuState.creg, undefinedWord);
      cpuState.areg = pointer;
      cpuState.breg = 0x1234;
      cpuState.creg = 0x5678;
      expect("store queue back succeeds", storeBack(0) != 0, 0);
      expect("store queue back value", omrState.ProcQueueBPtr[priority], pointer);
      expect("store queue back preserves front", omrState.ProcQueueFPtr[priority], pointer);
      expect("store queue back pops A", cpuState.areg, 0x1234);
      expect("store queue back shifts C into B", cpuState.breg, 0x5678);
      expect("store queue back leaves C undefined", cpuState.creg, undefinedWord);
      cpuState.areg = 0x152000;
      cpuState.breg = 0x1234;
      cpuState.creg = 0x5678;
      expect("save queue succeeds", saveQueue(0) != 0, 0);
      uint32_t value = 0;
      readBytes(0x152000, 4, &value);
      expect("save queue front", value, pointer);
      readBytes(0x152004, 4, &value);
      expect("save queue back", value, pointer);
      expect("save queue A pop", cpuState.areg, 0x1234);
      expect("save queue B pop", cpuState.breg, 0x5678);
      expect("save queue C undefined", cpuState.creg, undefinedWord);
      expect("queue controls preserve other front", omrState.ProcQueueFPtr[priority ^ 1U], NO_PROCESS);
      expect("queue controls preserve other back", omrState.ProcQueueBPtr[priority ^ 1U], NO_PROCESS);
    }
    cpuState.areg = 0xfffffffcU;
    expect("save queue wrapping address", saveQueue(0) != 0, 0);
    uint32_t value = 0;
    readBytes(0, 4, &value);
    expect("save queue wraps second word", value, NO_PROCESS);

    storeBytes(0x153000 - 4, 4, 0x7200);
    storeBytes(0x154000 - 4, 4, 0x7300);
    storeBytes(0x155000 - 4, 4, 0x7400);
    storeBytes(0x156000 - 4, 4, 0x7500);
    storeBytes(0x153000 - 8, 4, 0x154000);
    storeBytes(0x154000 - 8, 4, NO_PROCESS);
    cpuState.areg = priority;
    cpuState.breg = 0x153000;
    cpuState.creg = 0x154000;
    expect("swapqueue installs list", swapqueue_(0) != 0, 0);
    expect("swapqueue returns empty front", cpuState.areg, NO_PROCESS);
    expect("swapqueue returns empty back", cpuState.breg, NO_PROCESS);
    expect("swapqueue clears C", cpuState.creg, undefinedWord);
    storeBytes(0x155000 - 8, 4, 0x156000);
    storeBytes(0x156000 - 8, 4, NO_PROCESS);
    cpuState.areg = priority;
    cpuState.breg = 0x155000;
    cpuState.creg = 0x156000;
    expect("insertqueue prepends list", insertqueue_(0) != 0, 0);
    expect("insertqueue preserves old back", omrState.ProcQueueBPtr[priority], 0x154000);
    readBytes(0x156000 - 8, 4, &value);
    expect("insertqueue joins new tail to old front", value, 0x153000);
    expect("insertqueue A undefined", cpuState.areg, undefinedWord);
    expect("insertqueue B undefined", cpuState.breg, undefinedWord);
    expect("insertqueue C undefined", cpuState.creg, undefinedWord);
    cpuState.areg = priority;
    cpuState.breg = NO_PROCESS;
    cpuState.creg = 0xdeadbeef;
    expect("empty insert ignores back", insertqueue_(0) != 0, 0);
    expect("empty insert preserves front", omrState.ProcQueueFPtr[priority], 0x155000);
    expect("empty insert preserves back", omrState.ProcQueueBPtr[priority], 0x154000);
    cpuState.areg = priority;
    cpuState.breg = 0x157000;
    cpuState.creg = 0x157001;
    expect("insertqueue rejects malformed tail", insertqueue_(0) == INVALID_PROCESS_LIST, 1);
    expect("failed insert preserves front", omrState.ProcQueueFPtr[priority], 0x155000);
    expect("failed insert preserves back", omrState.ProcQueueBPtr[priority], 0x154000);
    cpuState.areg = priority;
    cpuState.breg = NO_PROCESS;
    cpuState.creg = NO_PROCESS;
    expect("swapqueue detaches list", swapqueue_(0) != 0, 0);
    expect("swapqueue returns old front", cpuState.areg, 0x155000);
    expect("swapqueue returns old back", cpuState.breg, 0x154000);
    cpuState.creg = cpuState.breg;
    cpuState.breg = cpuState.areg;
    cpuState.areg = priority;
    expect("insertqueue restores into empty queue", insertqueue_(0) != 0, 0);
    expect("insert into empty sets back", omrState.ProcQueueBPtr[priority], 0x154000);
    readBytes(0x154000 - 8, 4, &value);
    expect("insert into empty preserves tail link", value, NO_PROCESS);
    for (uint32_t workspace : {0x155000U, 0x156000U, 0x153000U, 0x154000U}) {
      cpuState.running = false;
      expect("inserted queue dispatches", pollTimers() != 0, 0);
      expect("inserted queue FIFO order", cpuState.wptr, workspace);
      expect("inserted queue dispatch priority", cpuState.priority, priority);
    }
    expect("inserted queue drains front", omrState.ProcQueueFPtr[priority], NO_PROCESS);
    expect("inserted queue drains back", omrState.ProcQueueBPtr[priority], NO_PROCESS);
  }
  schedulerFixture(0x150000);
  for (uint32_t priority : {2U, 256U, 0xffffffffU}) {
    cpuState.areg = priority;
    cpuState.breg = NO_PROCESS;
    cpuState.creg = NO_PROCESS;
    expect("swapqueue rejects invalid priority", swapqueue_(0) == INVALID_PROCESS_LIST, 1);
    expect("insertqueue rejects invalid priority", insertqueue_(0) == INVALID_PROCESS_LIST, 1);
    expect("invalid queue priority preserves A", cpuState.areg, priority);
  }
  schedulerFixture(0x150000);
  storeBytes(0x157000 - 4, 4, 0x7600);
  storeBytes(0x157000 - 8, 4, NO_PROCESS);
  cpuState.areg = HIGH_PRIORITY;
  cpuState.breg = 0x157000;
  cpuState.creg = 0x157000;
  cpuState.iptr = 0x7700;
  storeBytes(0x7700, 2, 0xf060);
  expect("encoded swapqueue decode", decodeNextInstr() != 0, 0);
  int watchFlag = 0;
  expect("encoded swapqueue executes", execInstr(&watchFlag) != 0, 0);
  expect("encoded swapqueue preempts after instruction", cpuState.wptr, 0x157000);
  expect("high queue process stops", stopp_(0) != 0, 0);
  expect("swapqueue low shadow restored", cpuState.wptr, 0x150000);
  expect("swapqueue low shadow A result", cpuState.areg, NO_PROCESS);
  expect("swapqueue low shadow B result", cpuState.breg, NO_PROCESS);
  expect("swapqueue low shadow next Iptr", cpuState.iptr, 0x7702);
  schedulerFixture(0x20000);
}

static void testParallelEnd() {
  for (unsigned char priority : {static_cast<unsigned char>(HIGH_PRIORITY), static_cast<unsigned char>(LOW_PRIORITY)}) {
    schedulerFixture(0x15b000, priority);
    storeBytes(0x15c000, 4, 0x7c00);
    storeBytes(0x15c004, 4, 3);
    storeBytes(0x15b000 - 4, 4, 0xdeadbeef);
    storeBytes(0x15d000 - 4, 4, 0x7d00);
    storeBytes(0x15e000 - 4, 4, 0x7e00);
    enqueueProcess(0x15d000, priority);
    enqueueProcess(0x15e000, priority);
    cpuState.areg = 0x15c000;
    expect("endp first branch retires", endp_(0) != 0, 0);
    expect("endp dispatches next branch", cpuState.wptr, 0x15d000);
    uint32_t value = 0;
    readBytes(0x15c004, 4, &value);
    expect("endp decrements branch count", value, 2);
    readBytes(0x15b000 - 4, 4, &value);
    expect("endp does not save retired Iptr", value, 0xdeadbeef);
    cpuState.areg = 0x15c000;
    expect("endp second branch retires", endp_(0) != 0, 0);
    expect("endp FIFO next branch", cpuState.wptr, 0x15e000);
    cpuState.areg = 0x15c000;
    expect("endp final branch enters successor", endp_(0) != 0, 0);
    expect("endp successor workspace", cpuState.wptr, 0x15c000);
    expect("endp successor entry", cpuState.iptr, 0x7c00);
    expect("endp preserves priority", cpuState.priority, priority);
    expect("endp successor running", cpuState.running, 1);
    expect("endp successor A undefined", cpuState.areg, undefinedWord);
    expect("endp successor B undefined", cpuState.breg, undefinedWord);
    expect("endp successor C undefined", cpuState.creg, undefinedWord);
    readBytes(0x15c004, 4, &value);
    expect("endp final count stays one", value, 1);
    expect("endp does not enqueue successor", omrState.ProcQueueFPtr[priority], NO_PROCESS);
    schedulerFixture(0x15b000, priority);
    storeBytes(0x15f004, 4, 2);
    cpuState.areg = 0x15f000;
    expect("endp nonfinal needs only count word", endp_(0) != 0, 0);
    expect("endp no runnable branch idles", cpuState.running, 0);
    expect("endp idle decoder reports idle", decodeNextInstr() == CPU_IDLE, 1);
  }
  for (uint32_t count : {0U, 0xffffffffU}) {
    schedulerFixture(0x15b000);
    storeBytes(0x160004, 4, count);
    cpuState.areg = 0x160000;
    expect("endp unsigned count executes", endp_(0) != 0, 0);
    uint32_t value = 0;
    readBytes(0x160004, 4, &value);
    expect("endp unsigned count wraps", value, count - 1U);
    expect("endp count has no integer error", omrState.HP_ErrorFlag | omrState.LP_ErrorFlag, 0);
  }
  schedulerFixture(0x15b000);
  cpuState.areg = 0x6f000000;
  cpuState.breg = 0x1234;
  cpuState.creg = 0x5678;
  expect("endp missing count fails", endp_(0) != 0, 1);
  expect("failed endp preserves running", cpuState.running, 1);
  expect("failed endp preserves workspace", cpuState.wptr, 0x15b000);
  expect("failed endp preserves A", cpuState.areg, 0x6f000000);
  expect("failed endp preserves B", cpuState.breg, 0x1234);
  expect("failed endp preserves C", cpuState.creg, 0x5678);
  storeBytes(0x6f010004, 4, 1);
  cpuState.areg = 0x6f010000;
  expect("endp missing successor fails", endp_(0) != 0, 1);
  expect("missing successor preserves Iptr", cpuState.iptr, 0x3000);
  expect("missing successor preserves workspace", cpuState.wptr, 0x15b000);
  cpuState.areg = 0x160001;
  expect("endp rejects unaligned successor", endp_(0) == BAD_WPTR, 1);
  storeBytes(0xfffffffcU, 4, 0x7f00);
  storeBytes(0, 4, 1);
  cpuState.areg = 0xfffffffcU;
  expect("endp count address wraps", endp_(0) != 0, 0);
  expect("endp wrapped successor workspace", cpuState.wptr, 0xfffffffcU);
  expect("endp wrapped successor entry", cpuState.iptr, 0x7f00);
  schedulerFixture(0x15b000);
  storeBytes(0x160000, 4, 0x8000);
  storeBytes(0x160004, 4, 1);
  cpuState.iptr = 0x8100;
  cpuState.areg = 0x160000;
  storeBytes(0x8100, 1, 0xf3);
  expect("encoded endp decode", decodeNextInstr() != 0, 0);
  int watchFlag = 0;
  expect("encoded endp executes", execInstr(&watchFlag) != 0, 0);
  expect("encoded endp enters successor", cpuState.wptr, 0x160000);
  expect("encoded endp sets absolute successor Iptr", cpuState.iptr, 0x8000);
  schedulerFixture(0x164000);
  cpuState.areg = 0x1234;
  cpuState.breg = 0x5678;
  cpuState.creg = 0x9abc;
  storeBytes(0x165000 - 4, 4, 0x8400);
  storeBytes(0x166004, 4, 2);
  expect("endp preemption fixture queues high branch", enqueueProcess(0x165000, HIGH_PRIORITY) != 0, 0);
  expect("endp preemption fixture dispatches high branch", pollTimers() != 0, 0);
  expect("endp high branch runs", cpuState.wptr, 0x165000);
  cpuState.areg = 0x166000;
  expect("endp high branch retires to low shadow", endp_(0) != 0, 0);
  expect("endp restores interrupted low workspace", cpuState.wptr, 0x164000);
  expect("endp restores low priority", cpuState.priority, LOW_PRIORITY);
  expect("endp restores low A", cpuState.areg, 0x1234);
  expect("endp restores low B", cpuState.breg, 0x5678);
  expect("endp restores low C", cpuState.creg, 0x9abc);
  expect("endp restores low Iptr", cpuState.iptr, 0x3000);
  schedulerFixture(0x20000);
}

static void testInterruptAndTimesliceControls() {
  schedulerFixture(0x170000);
  cpuState.status = STATUS_TIMESLICE_ENABLE;
  cpuState.areg = 0x1234;
  cpuState.breg = 0x5678;
  cpuState.creg = 0x9abc;
  uint32_t enables = omrState.Enables;
  expect("intdis succeeds", intdis_(0) != 0, 0);
  expect("intdis preserves A", cpuState.areg, 0x1234);
  expect("intdis preserves B", cpuState.breg, 0x5678);
  expect("intdis preserves C", cpuState.creg, 0x9abc);
  expect("intdis preserves global enables", omrState.Enables, enables);
  storeBytes(0x171000 - 4, 4, 0x8700);
  enqueueProcess(0x171000, HIGH_PRIORITY);
  expect("intdis defers queued high process", pollTimers() != 0, 0);
  expect("disabled interrupt keeps low process", cpuState.wptr, 0x170000);
  cpuState.iptr = 0x8800;
  storeBytes(0x8800, 2, 0xf52c);
  expect("encoded intenb decode", decodeNextInstr() != 0, 0);
  int watchFlag = 0;
  expect("encoded intenb permits boundary preemption", execInstr(&watchFlag) != 0, 0);
  expect("intenb high process runs", cpuState.wptr, 0x171000);
  stopp_(0);
  expect("intenb restores completed low context", cpuState.wptr, 0x170000);
  expect("intenb completes before preemption", cpuState.iptr, 0x8802);
  expect("intenb low A preserved", cpuState.areg, 0x1234);
  intdis_(0);
  cpuState.areg = 0;
  expect("disable process timeslicing", settimeslice_(0) != 0, 0);
  expect("settimeslice returns prior enabled", cpuState.areg, 1);
  expect("settimeslice clears Status enable", cpuState.status & STATUS_TIMESLICE_ENABLE, 0);
  cpuState.areg = 1;
  settimeslice_(0);
  expect("settimeslice returns prior disabled", cpuState.areg, 0);
  expect("settimeslice restores Status enable", cpuState.status & STATUS_TIMESLICE_ENABLE, STATUS_TIMESLICE_ENABLE);
  expect("settimeslice preserves interruption disable", cpuState.interruptsDisabled, 1);
  storeBytes(0x172000 - 4, 4, 0x8900);
  enqueueProcess(0x172000, HIGH_PRIORITY);
  expect("descheduling clears interruption disable", stopp_(0) != 0, 0);
  expect("descheduling releases waiting high process", cpuState.wptr, 0x172000);
  expect("descheduled disable is not inherited", cpuState.interruptsDisabled, 0);
  for (unsigned char priority : {static_cast<unsigned char>(HIGH_PRIORITY), static_cast<unsigned char>(LOW_PRIORITY)}) {
    schedulerFixture(0x173000, priority);
    cpuState.status = 0;
    cpuState.interruptsDisabled = priority == LOW_PRIORITY;
    expect("empty timeslice succeeds", timeslice_(0) != 0, 0);
    expect("empty timeslice preserves workspace", cpuState.wptr, 0x173000);
    expect("empty timeslice preserves disable", cpuState.interruptsDisabled, priority == LOW_PRIORITY);
    expect("empty timeslice preserves enable state", cpuState.status & STATUS_TIMESLICE_ENABLE, 0);
    expect("empty timeslice A undefined", cpuState.areg, undefinedWord);
    storeBytes(0x174000 - 4, 4, 0x8a00);
    storeBytes(0x175000 - 4, 4, 0x8b00);
    enqueueProcess(0x174000, priority);
    enqueueProcess(0x175000, priority);
    cpuState.iptr = 0x8c00;
    storeBytes(0x8c00, 2, 0xf360);
    expect("encoded timeslice decode", decodeNextInstr() != 0, 0);
    expect("explicit timeslice ignores process disables", execInstr(&watchFlag) != 0, 0);
    expect("timeslice runs first FIFO process", cpuState.wptr, 0x174000);
    expect("timeslice retains second FIFO process", omrState.ProcQueueFPtr[priority], 0x175000);
    expect("timeslice appends caller to back", omrState.ProcQueueBPtr[priority], 0x173000);
    uint32_t value = 0;
    readBytes(0x173000 - 4, 4, &value);
    expect("timeslice saves next-instruction Iptr", value, 0x8c02);
    stopp_(0);
    expect("timeslice FIFO second process runs", cpuState.wptr, 0x175000);
    stopp_(0);
    expect("timeslice caller resumes last", cpuState.wptr, 0x173000);
    expect("timeslice resumed Iptr", cpuState.iptr, 0x8c02);
    expect("timeslice resumed interruption enabled", cpuState.interruptsDisabled, 0);
    expect("timeslice resumed timeslicing enabled", cpuState.status & STATUS_TIMESLICE_ENABLE, STATUS_TIMESLICE_ENABLE);
    expect("timeslice resumed A undefined", cpuState.areg, undefinedWord);
  }
  schedulerFixture(0x173000, HIGH_PRIORITY);
  cpuState.areg = 0;
  cpuState.status = STATUS_TIMESLICE_ENABLE;
  intdis_(0);
  expect("high intdis ignored", cpuState.interruptsDisabled, 0);
  settimeslice_(0);
  expect("high settimeslice ignored", cpuState.status & STATUS_TIMESLICE_ENABLE, STATUS_TIMESLICE_ENABLE);
  expect("high settimeslice preserves A", cpuState.areg, 0);
  schedulerFixture(0x17b000);
  omrState.Enables &= ~static_cast<uint32_t>(LP_PROCESS_INT_ENB);
  storeBytes(0x17c000 - 4, 4, 0x9000);
  enqueueProcess(0x17c000, HIGH_PRIORITY);
  intdis_(0);
  intenb_(0);
  expect("intenb respects global interrupt mask", pollTimers() != 0, 0);
  expect("globally masked high process remains queued", cpuState.wptr, 0x17b000);
  expect("intenb does not modify global interrupt bit", omrState.Enables & LP_PROCESS_INT_ENB, 0);
  schedulerFixture(0x20000);
}

static void testTimerScheduler() {
  for (unsigned char priority : {static_cast<unsigned char>(HIGH_PRIORITY), static_cast<unsigned char>(LOW_PRIORITY)}) {
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
      bool alternative = (scenario & 1U) != 0;
      bool existingTimer = (scenario & 2U) != 0;
      constexpr uint32_t workspace = 0x260000;
      schedulerFixture(workspace);
      cpuState.priority = priority;
      omrState.ClockRegHP = omrState.ClockRegLP = 10;
      uint32_t front = existingTimer ? 0x261000U : NO_PROCESS;
      if (existingTimer) {
        storeBytes(front - 20, 4, 16);
        storeBytes(front - 16, 4, NO_PROCESS);
        omrState.TptrReg[priority] = front;
        omrState.TnextReg[priority] = 16;
      }
      uint32_t alarm = omrState.TnextReg[priority];
      MemoryDevice failedIptr{workspace - 4, workspace - 1, nullptr, nullptr,
          +[](void *, uint32_t, int, uint32_t) { return WRITE_FAILURE; }};
      expect("timer save-failure device registers", registerMemoryDevice(failedIptr) != 0, 0);
      int result = 0;
      if (alternative) {
        talt_(0);
        cpuState.areg = 1;
        cpuState.breg = 20;
        enbt_(0);
        result = taltwt_(0);
      } else {
        cpuState.areg = 20;
        result = tin_(0);
      }
      expect("timer wait propagates resume-save failure", result == WRITE_FAILURE, 1);
      expect("failed timer save preserves running process", cpuState.running, 1);
      expect("failed timer save preserves instruction pointer", cpuState.iptr, 0x3000);
      expect("failed timer save does not publish waiter", omrState.TptrReg[priority], front);
      expect("failed timer save does not change alarm", omrState.TnextReg[priority], alarm);
      if (existingTimer) {
        uint32_t next = 0;
        readBytes(front - 16, 4, &next);
        expect("failed timer save preserves existing tail", next, NO_PROCESS);
      }
      if (alternative) {
        uint32_t state = 0;
        readBytes(workspace - 12, 4, &state);
        expect("failed timed ALT save preserves enabling state", state, ALT_ENABLING);
      }
      expect("timer save-failure device unregisters", unregisterMemoryDevice(failedIptr.start) != 0, 0);
    }
  }
  schedulerFixture(0x20000);
  omrState.ClockRegLP = 10;
  reset(10, 0, 0);
  omrState.ClockRegLP = 10;
  omrState.ClockEnables = 3;
  omrState.Enables = 0xffffc000;
  expect("tin schedules equal time", tin_(0) != 0, 0);
  expect("tin suspends process", cpuState.running, 0);
  expect("tin timer head", omrState.TptrReg[LOW_PRIORITY], 0x20000);
  expect("tin alarm is requested time plus one", omrState.TnextReg[LOW_PRIORITY], 11);
  uint32_t value = 0;
  readBytes(0x20000 - 20, 4, &value);
  expect("tin saved deadline", value, 11);
  advanceTimers(3887);
  expect("tin does not wake before deadline", cpuState.running, 0);
  advanceTimers(1);
  expect("tin wakes at deadline", cpuState.running, 1);
  expect("tin resumes workspace", cpuState.wptr, 0x20000);
  expect("tin restores instruction pointer", cpuState.iptr, 0x3000);
  expect("tin removes expired timer", omrState.TptrReg[LOW_PRIORITY], NO_PROCESS);

  schedulerFixture(0x21000);
  cpuState.areg = 50;
  tin_(0);
  expect("idle timer fetch resumes", decodeNextInstr() != 0, 0);
  expect("idle advances only until deadline", omrState.ClockRegLP, 51);
  expect("idle advances both clock rates", omrState.ClockRegHP, 3264);
  expect("idle fetches resumed instruction", instrEntry[instrCode].function == nop_, 1);

  schedulerFixture(0x22000);
  omrState.ClockRegLP = 0xfffffffe;
  cpuState.areg = 0xffffffff;
  tin_(0);
  advanceTimers(2U * 3888U);
  expect("timer deadline wraps", omrState.ClockRegLP, 0);
  expect("wrapped deadline wakes process", cpuState.running, 1);

  schedulerFixture(0x23000);
  cpuState.areg = 1;
  tin_(0);
  omrState.ClockEnables = 0;
  expect("idle with disabled timers returns control", decodeNextInstr() == CPU_IDLE, 1);
  omrState.ClockEnables = 3;
  expect("reenabled timer resumes idle CPU", decodeNextInstr() != 0, 0);

  schedulerFixture(0x24000);
  cpuState.areg = 1;
  tin_(0);
  omrState.Enables &= ~static_cast<uint32_t>(LP_TIMER_ALRM_ENB);
  advanceTimers(3U * 3888U);
  expect("masked timer alarm retains waiter", cpuState.running, 0);
  omrState.Enables |= LP_TIMER_ALRM_ENB;
  pollTimers();
  expect("unmasked pending alarm wakes waiter", cpuState.running, 1);

  schedulerFixture(0x25000);
  reset(0x26000, 0x20, 0);
  omrState.Enables = 0xffffc000;
  startp_(0);
  cpuState.areg = 0x27000;
  cpuState.breg = 0x40;
  cpuState.creg = 0;
  omrState.Enables = 0xffffc000;
  startp_(0);
  expect("ready queue FIFO front", omrState.ProcQueueFPtr[LOW_PRIORITY], 0x26000);
  expect("ready queue FIFO back", omrState.ProcQueueBPtr[LOW_PRIORITY], 0x27000);
  stopp_(0);
  expect("stop dispatches first ready process", cpuState.wptr, 0x26000);
  expect("startp computes new entry address", cpuState.iptr, 0x3020);
  stopp_(0);
  expect("stop dispatches second ready process", cpuState.wptr, 0x27000);
  expect("second process entry address", cpuState.iptr, 0x3040);
  stopp_(0);
  expect("empty ready queue leaves CPU idle", cpuState.running, 0);

  schedulerFixture(0x28000);
  cpuState.areg = 0x1234;
  cpuState.breg = 0x5678;
  cpuState.creg = 0x9abc;
  storeBytes(0x29000 - 4, 4, 0x4000);
  enqueueProcess(0x29000, HIGH_PRIORITY);
  pollTimers();
  expect("high priority preempts low", cpuState.priority, HIGH_PRIORITY);
  expect("high priority workspace selected", cpuState.wptr, 0x29000);
  stopp_(0);
  expect("low priority shadow resumes", cpuState.priority, LOW_PRIORITY);
  expect("low priority workspace preserved", cpuState.wptr, 0x28000);
  expect("low shadow A preserved", cpuState.areg, 0x1234);
  expect("low shadow B preserved", cpuState.breg, 0x5678);
  expect("low shadow C preserved", cpuState.creg, 0x9abc);
  schedulerFixture(0x20000);
}

static void testTimerOrderingAndPreemption() {
  schedulerFixture(0x40000);
  cpuState.areg = 20;
  tin_(0);
  cpuState.running = true;
  cpuState.wptr = 0x41000;
  cpuState.iptr = 0x3100;
  cpuState.areg = 5;
  tin_(0);
  cpuState.running = true;
  cpuState.wptr = 0x42000;
  cpuState.iptr = 0x3200;
  cpuState.areg = 5;
  tin_(0);
  expect("timer list sorted head", omrState.TptrReg[LOW_PRIORITY], 0x41000);
  expect("timer list earliest alarm", omrState.TnextReg[LOW_PRIORITY], 6);
  uint32_t value = 0;
  readBytes(0x41000 - 16, 4, &value);
  expect("equal timer deadlines use FIFO", value, 0x42000);
  readBytes(0x42000 - 16, 4, &value);
  expect("later timer follows equal deadlines", value, 0x40000);
  advanceTimers(6U * 3888U);
  expect("first equal-deadline waiter runs", cpuState.wptr, 0x41000);
  expect("second equal-deadline waiter queued", omrState.ProcQueueFPtr[LOW_PRIORITY], 0x42000);
  expect("later waiter still timed", omrState.TptrReg[LOW_PRIORITY], 0x40000);
  stopp_(0);
  expect("second equal-deadline waiter runs", cpuState.wptr, 0x42000);
  cpuState.areg = 21;
  sttimer_(0);
  expect("sttimer services newly due alarm", omrState.TptrReg[LOW_PRIORITY], NO_PROCESS);
  expect("sttimer queues ready waiter", omrState.ProcQueueFPtr[LOW_PRIORITY], 0x40000);

  schedulerFixture(0x43000);
  omrState.TptrReg[HIGH_PRIORITY] = 0x44000;
  omrState.TnextReg[HIGH_PRIORITY] = 99;
  storeBytes(0x45000 - 20, 4, 9);
  storeBytes(0x45000 - 16, 4, NO_PROCESS);
  cpuState.areg = HIGH_PRIORITY;
  cpuState.breg = 0x45000;
  expect("swaptimer accepts new list", swaptimer_(0) != 0, 0);
  expect("swaptimer returns previous list", cpuState.areg, 0x44000);
  expect("swaptimer installs front", omrState.TptrReg[HIGH_PRIORITY], 0x45000);
  expect("swaptimer updates alarm", omrState.TnextReg[HIGH_PRIORITY], 9);
  cpuState.areg = HIGH_PRIORITY;
  cpuState.breg = NO_PROCESS;
  swaptimer_(0);
  expect("swaptimer detaches list", omrState.TptrReg[HIGH_PRIORITY], NO_PROCESS);
  expect("empty swaptimer leaves alarm register unchanged", omrState.TnextReg[HIGH_PRIORITY], 9);

  schedulerFixture(0x46000);
  cpuState.areg = 0;
  tin_(0);
  omrState.Enables |= TIMER_TRAPENB;
  expect("enabled timer trap requires valid handler", advanceTimers(3888) == INVALID_TRAP_HANDLER, 1);
  expect("unsupported timer trap keeps queued process", omrState.TptrReg[LOW_PRIORITY], 0x46000);
  omrState.Enables &= ~static_cast<uint32_t>(TIMER_TRAPENB);
  pollTimers();
  expect("pending timer resumes when trap disabled", cpuState.running, 1);

  schedulerFixture(0x47000);
  cpuState.iptr = 0x3400;
  cpuState.areg = 0x1234;
  cpuState.breg = 0x5678;
  cpuState.creg = 0x9abc;
  storeBytes(0x3400, 1, 0x41);
  storeBytes(0x48000 - 4, 4, 0x3500);
  storeBytes(0x48000 - 20, 4, 1);
  storeBytes(0x48000 - 16, 4, NO_PROCESS);
  uint32_t previous = 0;
  swapTimerList(HIGH_PRIORITY, 0x48000, &previous);
  omrState.ClockPhaseHP = CPU_CLOCK - HP_TIMER_HZ;
  decodeNextInstr();
  int breakFlag = 0;
  expect("timer interrupts at instruction boundary", execInstr(&breakFlag) != 0, 0);
  expect("due high timer preempts low", cpuState.wptr, 0x48000);
  stopp_(0);
  expect("preempted instruction finished before switch", cpuState.areg, 1);
  expect("preempted instruction shifted A correctly", cpuState.breg, 0x1234);
  expect("preempted instruction shifted B correctly", cpuState.creg, 0x5678);
  expect("preempted instruction advanced Iptr", cpuState.iptr, 0x3401);

  schedulerFixture(0x4f000);
  cpuState.iptr = 0x3600;
  cpuState.areg = 0x777;
  storeBytes(0x3600, 1, 0x41);
  storeBytes(0x50000 - 4, 4, 0x3700);
  storeBytes(0x50000 - 20, 4, 1);
  storeBytes(0x50000 - 16, 4, NO_PROCESS);
  swapTimerList(HIGH_PRIORITY, 0x50000, &previous);
  omrState.ClockPhaseHP = CPU_CLOCK - HP_TIMER_HZ;
  decodeNextInstr();
  changeTrapMask(HIGH_PRIORITY, TIMER_TRAPENB, true, &previous);
  expect("timer failure before execution is reported", execInstr(&breakFlag) == INVALID_TRAP_HANDLER, 1);
  expect("timer failure does not skip instruction", cpuState.iptr, 0x3600);
  expect("timer failure does not execute instruction", cpuState.areg, 0x777);

  schedulerFixture(0x49000, HIGH_PRIORITY);
  cpuState.areg = 0;
  tin_(0);
  expect("large elapsed batch services old deadline", advanceTimers(uint64_t{CPU_CLOCK} * 3000) != 0, 0);
  expect("elapsed ticks not modulo comparison alone wake process", cpuState.running, 1);
  schedulerFixture(0x20000);
}

static void testSchedulerSnapshot() {
  char directory[] = "/tmp/st20-runtime-snapshot-XXXXXX";
  if (!mkdtemp(directory)) {
    expect("runtime snapshot temporary directory", 0, 1);
    return;
  }
  schedulerFixture(0x31000);
  omrState.ClockRegLP = 10;
  cpuState.areg = 50;
  tin_(0);
  cpuState.running = true;
  cpuState.wptr = 0x32000;
  cpuState.iptr = 0x4000;
  cpuState.areg = 0x1111;
  cpuState.breg = 0x2222;
  cpuState.creg = 0x3333;
  omrState.ClockRegHP = 20;
  omrState.ClockPhaseHP = 123;
  omrState.ClockPhaseLP = 456;
  cpuState.move2dBlockLength = 3;
  cpuState.move2dDestStride = 9;
  cpuState.move2dSourceStride = 7;
  storeBytes(0x33000 - 4, 4, 0x5000);
  enqueueProcess(0x33000, HIGH_PRIORITY);
  pollTimers();
  cpuState.move2dBlockLength = 2;
  cpuState.move2dDestStride = 5;
  cpuState.move2dSourceStride = 4;
  expect("runtime snapshot save", saveCPUState(directory) != 0, 0);
  resetScheduler();
  omrState = {};
  cpuState.priority = LOW_PRIORITY;
  cpuState.wptr = 0;
  expect("runtime snapshot restore", loadCPUState(directory) != 0, 0);
  expect("snapshot active priority", cpuState.priority, HIGH_PRIORITY);
  expect("snapshot active workspace", cpuState.wptr, 0x33000);
  expect("snapshot HP clock", omrState.ClockRegHP, 20);
  expect("snapshot LP clock", omrState.ClockRegLP, 10);
  expect("snapshot HP phase", omrState.ClockPhaseHP, 123);
  expect("snapshot LP phase", omrState.ClockPhaseLP, 456);
  expect("snapshot timer queue", omrState.TptrReg[LOW_PRIORITY], 0x31000);
  expect("snapshot active move2d rows", cpuState.move2dBlockLength, 2);
  expect("snapshot active move2d destination stride", cpuState.move2dDestStride, 5);
  expect("snapshot active move2d source stride", cpuState.move2dSourceStride, 4);
  stopp_(0);
  expect("snapshot restored low shadow workspace", cpuState.wptr, 0x32000);
  expect("snapshot restored low shadow A", cpuState.areg, 0x1111);
  expect("snapshot restored low shadow B", cpuState.breg, 0x2222);
  expect("snapshot restored low shadow C", cpuState.creg, 0x3333);
  expect("snapshot low shadow move2d rows", cpuState.move2dBlockLength, 3);
  expect("snapshot low shadow move2d destination stride", cpuState.move2dDestStride, 9);
  expect("snapshot low shadow move2d source stride", cpuState.move2dSourceStride, 7);
  advanceTimers(41U * 3888U);
  expect("snapshot restored waiter becomes ready", omrState.ProcQueueFPtr[LOW_PRIORITY], 0x31000);
  stopp_(0);
  expect("snapshot waiter resumes", cpuState.wptr, 0x31000);
  std::string filename = std::string(directory) + "/cpu.bin";
  FILE *file = std::fopen(filename.c_str(), "r+b");
  if (file) {
    std::fseek(file, -static_cast<int64_t>(cpuRuntimeSnapshotSize()), SEEK_END);
    uint32_t badMagic = 0;
    std::fwrite(&badMagic, sizeof(badMagic), 1, file);
    std::fclose(file);
    expect("snapshot rejects invalid runtime header", loadCPUState(directory) == INVALID_CPU_FILE, 1);
  } else expect("snapshot corruption fixture open", 0, 1);
  expect("snapshot rewrite for version-1 fixture", saveCPUState(directory) != 0, 0);
  file = std::fopen(filename.c_str(), "r+b");
  if (file) {
    std::fseek(file, 0, SEEK_END);
    int64_t size = std::ftell(file);
    int64_t prefixSize = size - static_cast<int64_t>(cpuRuntimeSnapshotSize());
    std::fseek(file, prefixSize + static_cast<uint32_t>(sizeof(uint32_t)), SEEK_SET);
    uint32_t version = 1;
    std::fwrite(&version, sizeof(version), 1, file);
    std::fflush(file);
    expect("snapshot version-1 fixture truncation", ftruncate(fileno(file), prefixSize + 33 * static_cast<uint32_t>(sizeof(uint32_t))) != 0, 0);
    std::fclose(file);
    cpuState.move2dBlockLength = 99;
    cpuState.move2dDestStride = 88;
    cpuState.move2dSourceStride = 77;
    expect("reject version-1 snapshot", loadCPUState(directory) == INVALID_CPU_FILE, 1);
    expect("version-1 rejection preserves move2d rows", cpuState.move2dBlockLength, 99);
    expect("version-1 rejection preserves destination stride", cpuState.move2dDestStride, 88);
    expect("version-1 rejection preserves source stride", cpuState.move2dSourceStride, 77);
  } else expect("version-1 fixture open", 0, 1);
  std::remove(filename.c_str());
  rmdir(directory);
  schedulerFixture(0x20000);
}

static void testSemaphores() {
  schedulerFixture(0x60000);
  storeBytes(0x61000, 4, 2);
  cpuState.areg = 0x61000;
  expect("semaphore consumes permit without queue reads", wait_(0) != 0, 0);
  uint32_t value = 0;
  readBytes(0x61000, 4, &value);
  expect("semaphore decremented count", value, 1);
  expect("semaphore permit keeps process running", cpuState.running, 1);

  storeBytes(0x61000, 4, 0);
  storeBytes(0x61004, 4, NO_PROCESS);
  cpuState.areg = 0x61000;
  cpuState.iptr = 0x6000;
  expect("semaphore first waiter", wait_(0) != 0, 0);
  expect("semaphore wait suspends", cpuState.running, 0);
  readBytes(0x61004, 4, &value);
  expect("semaphore stores process descriptor", value, 0x60001);
  cpuState.running = true;
  cpuState.wptr = 0x62000;
  cpuState.iptr = 0x6200;
  cpuState.areg = 0x61000;
  wait_(0);
  readBytes(0x60000 - 8, 4, &value);
  expect("semaphore FIFO link retains priority", value, 0x62001);
  readBytes(0x61008, 4, &value);
  expect("semaphore FIFO tail", value, 0x62001);
  cpuState.running = true;
  cpuState.wptr = 0x63000;
  cpuState.iptr = 0x6300;
  cpuState.areg = 0x61000;
  signal_(0);
  expect("signal queues first waiter", omrState.ProcQueueFPtr[LOW_PRIORITY], 0x60000);
  readBytes(0x61004, 4, &value);
  expect("signal advances semaphore front", value, 0x62001);
  cpuState.areg = 0x61000;
  signal_(0);
  readBytes(0x61004, 4, &value);
  expect("signal empties semaphore list", value, NO_PROCESS);
  readBytes(0x61008, 4, &value);
  expect("signal clears last semaphore tail", value, NO_PROCESS);
  readBytes(0x61000, 4, &value);
  expect("signal waking waiters does not add permits", value, 0);
  stopp_(0);
  expect("first semaphore waiter resumes", cpuState.wptr, 0x60000);
  expect("first waiter saved Iptr", cpuState.iptr, 0x6000);
  stopp_(0);
  expect("second semaphore waiter resumes", cpuState.wptr, 0x62000);

  schedulerFixture(0x64000, HIGH_PRIORITY);
  storeBytes(0x65000, 4, 0);
  storeBytes(0x65004, 4, NO_PROCESS);
  cpuState.areg = 0x65000;
  wait_(0);
  cpuState.running = true;
  cpuState.priority = LOW_PRIORITY;
  cpuState.wptr = 0x66000;
  cpuState.areg = 0x65000;
  uint32_t previousMask = 0;
  changeTrapMask(HIGH_PRIORITY, SIGNAL_TRAPENB, true, &previousMask);
  expect("signal trap requires valid handler", signal_(0) == INVALID_TRAP_HANDLER, 1);
  readBytes(0x65004, 4, &value);
  expect("signal trap retains waiter", value, 0x64000);
  changeTrapMask(HIGH_PRIORITY, SIGNAL_TRAPENB, false, &previousMask);
  cpuState.areg = 0x65000;
  signal_(0);
  expect("signal preserves waiter priority", cpuState.priority, HIGH_PRIORITY);
  expect("high priority semaphore waiter preempts", cpuState.wptr, 0x64000);
  stopp_(0);
  expect("signal caller resumes from shadow", cpuState.wptr, 0x66000);

  storeBytes(0x67000, 4, 0xffffffff);
  storeBytes(0x67004, 4, NO_PROCESS);
  omrState.Enables |= SIGNAL_TRAPENB;
  cpuState.areg = 0x67000;
  expect("signal without waiter does not trigger trap", signal_(0) != 0, 0);
  readBytes(0x67000, 4, &value);
  expect("semaphore count increment wraps", value, 0);
  schedulerFixture(0x20000);
}

static void testInternalChannels() {
  schedulerFixture(0x70000);
  storeBytes(0x71000, 4, NO_PROCESS);
  for (uint32_t offset = 0; offset < 8; ++offset) storeBytes(0x72000 + offset, 1, 0xa0 + offset);
  cpuState.areg = 8;
  cpuState.breg = 0x71000;
  cpuState.creg = 0x74000;
  expect("channel receiver blocks", in_(0) != 0, 0);
  expect("channel receiver is suspended", cpuState.running, 0);
  uint32_t value = 0;
  readBytes(0x71000, 4, &value);
  expect("channel stores waiting descriptor", value, 0x70001);
  cpuState.running = true;
  cpuState.priority = HIGH_PRIORITY;
  cpuState.wptr = 0x75000;
  cpuState.areg = 8;
  cpuState.breg = 0x71000;
  cpuState.creg = 0x72000;
  expect("channel sender joins waiting receiver", out_(0) != 0, 0);
  for (uint32_t offset = 0; offset < 8; ++offset) {
    readBytes(0x74000 + offset, 1, &value);
    expect("channel receiver-first data", value, 0xa0 + offset);
  }
  readBytes(0x71000, 4, &value);
  expect("channel emptied after communication", value, NO_PROCESS);
  expect("channel receiver queued at original priority", omrState.ProcQueueFPtr[LOW_PRIORITY], 0x70000);
  stopp_(0);
  expect("channel receiver resumes", cpuState.wptr, 0x70000);

  schedulerFixture(0x76000);
  storeBytes(0x7a000, 4, NO_PROCESS);
  storeBytes(0x76000, 4, 0xdeadbeef);
  cpuState.iptr = 0x3600;
  cpuState.areg = 8;
  cpuState.breg = 0x7a000;
  cpuState.creg = 0x72000;
  expect("channel sender blocks", out_(0) != 0, 0);
  readBytes(0x76000, 4, &value);
  expect("general output preserves workspace Temp", value, 0xdeadbeef);
  cpuState.running = true;
  cpuState.wptr = 0x77000;
  cpuState.areg = 8;
  cpuState.breg = 0x7a000;
  cpuState.creg = 0x79000;
  expect("channel receiver joins waiting sender", in_(0) != 0, 0);
  for (uint32_t offset = 0; offset < 8; ++offset) {
    readBytes(0x79000 + offset, 1, &value);
    expect("channel sender-first data", value, 0xa0 + offset);
  }
  stopp_(0);
  expect("channel sender resumes", cpuState.wptr, 0x76000);
  expect("channel saved sender Iptr", cpuState.iptr, 0x3600);

  int (*outputs[])(uint32_t) = {outbyte_, outword_};
  const uint32_t lengths[] = {1, 4};
  const uint32_t expected[] = {0xef, 0x89abcdef};
  for (int index = 0; index < 2; ++index) {
    schedulerFixture(0x7b000);
    storeBytes(0x7c000, 4, NO_PROCESS);
    cpuState.areg = 0x89abcdef;
    cpuState.breg = 0x7c000;
    outputs[index](0);
    readBytes(0x7b000, static_cast<int>(lengths[index]), &value);
    expect("scalar output buffer survives blocking", value, expected[index]);
    cpuState.running = true;
    cpuState.wptr = 0x7d000;
    cpuState.areg = lengths[index];
    cpuState.breg = 0x7c000;
    cpuState.creg = 0x7e000;
    in_(0);
    readBytes(0x7e000, static_cast<int>(lengths[index]), &value);
    expect("scalar channel output bytes", value, expected[index]);
  }

  schedulerFixture(0x80000);
  storeBytes(0x80100, 4, NO_PROCESS);
  cpuState.areg = 0;
  cpuState.breg = 0x80100;
  cpuState.creg = 0x70004000;
  out_(0);
  cpuState.running = true;
  cpuState.wptr = 0x80200;
  cpuState.areg = 0;
  cpuState.breg = 0x80100;
  cpuState.creg = 0x70005000;
  expect("zero-length channel still synchronizes", in_(0) != 0, 0);
  expect("zero-length sender is rescheduled", omrState.ProcQueueFPtr[LOW_PRIORITY], 0x80000);
  expect("zero-length channel does not touch buffers", readBytes(0x70005000, 1, &value) == READ_UNUSED_MEM, 1);

  schedulerFixture(0x81000);
  storeBytes(0x82000, 4, NO_PROCESS);
  storeBytes(0x83000, 4, 0xeeeeeeee);
  cpuState.areg = 4;
  cpuState.breg = 0x82000;
  cpuState.creg = 0x83000;
  in_(0);
  cpuState.running = true;
  cpuState.wptr = 0x84000;
  storeBytes(0x84000, 4, 0xdeadbeef);
  cpuState.areg = 0x11223344;
  cpuState.breg = 0x82000;
  omrState.Enables |= INTERNALCH_TRAPENB;
  expect("channel trap requires valid handler", outword_(0) == INVALID_TRAP_HANDLER, 1);
  readBytes(0x83000, 4, &value);
  expect("channel trap preserves destination", value, 0xeeeeeeee);
  readBytes(0x82000, 4, &value);
  expect("channel trap preserves pending peer", value, 0x81001);
  readBytes(0x84000, 4, &value);
  expect("channel trap precedes scalar Temp write", value, 0xdeadbeef);

  cpuState.areg = 0x86000;
  cpuState.breg = 0x1111;
  cpuState.creg = 0x2222;
  expect("resetch initializes unused RAM", resetch_(0) != 0, 0);
  readBytes(0x86000, 4, &value);
  expect("resetch creates empty channel", value, NO_PROCESS);
  expect("resetch preserves B", cpuState.breg, 0x1111);
  expect("resetch preserves C", cpuState.creg, 0x2222);
  cpuState.areg = 0x82000;
  resetch_(0);
  expect("resetch returns old waiting descriptor", cpuState.areg, 0x81001);
  readBytes(0x82000, 4, &value);
  expect("resetch clears waiting channel", value, NO_PROCESS);
  expect("resetch does not enqueue cancelled waiter", omrState.ProcQueueFPtr[LOW_PRIORITY], NO_PROCESS);
  cpuState.areg = 0;
  cpuState.breg = 0x20000000;
  cpuState.creg = 0;
  expect("hardware channel not treated as RAM rendezvous", in_(0) == CHANNEL_HARDWARE_UNSUPPORTED, 1);
  cpuState.areg = 0x80000000;
  expect("core-reserved channel reset rejected", resetch_(0) == CHANNEL_HARDWARE_UNSUPPORTED, 1);
  schedulerFixture(0x20000);
}

static void testMove2d() {
  int (*copies[])(uint32_t) = {move2dall_, move2dnonzero_, move2dzero_};
  const uint32_t source = 0x90000;
  const uint32_t destination = 0x92000;
  for (int mode = 0; mode < 3; ++mode) {
    for (uint32_t offset = 0; offset < 27; ++offset) storeBytes(destination + offset, 1, 0xaa);
    for (uint32_t row = 0; row < 3; ++row) {
      for (uint32_t column = 0; column < 5; ++column) {
        uint32_t value = column % 2 == 0 ? 0 : row * 10 + column;
        storeBytes(source + row * 7 + column, 1, static_cast<uint32_t>(value));
      }
    }
    cpuState.areg = 3;
    cpuState.breg = 9;
    cpuState.creg = 7;
    move2dinit_(0);
    expect("move2dinit rows", cpuState.move2dBlockLength, 3);
    expect("move2dinit destination stride", cpuState.move2dDestStride, 9);
    expect("move2dinit source stride", cpuState.move2dSourceStride, 7);
    cpuState.areg = 5;
    cpuState.breg = destination;
    cpuState.creg = source;
    expect("move2d copy succeeds", copies[mode](0) != 0, 0);
    for (uint32_t row = 0; row < 3; ++row) {
      for (uint32_t column = 0; column < 9; ++column) {
        uint32_t value = column % 2 == 0 ? 0 : row * 10 + column;
        bool copied = column < 5 && (mode == 0 || (mode == 1 && value != 0) ||
                                    (mode == 2 && value == 0));
        uint32_t actual = 0;
        readBytes(destination + row * 9 + column, 1, &actual);
        expect("move2d data/filter/padding", actual, copied ? value : 0xaa);
      }
    }
    expect("move2d clears A", cpuState.areg, undefinedWord);
    expect("move2d clears B", cpuState.breg, undefinedWord);
    expect("move2d clears C", cpuState.creg, undefinedWord);
  }
  struct FilteredMoveBus {
    unsigned char source[24]{};
    unsigned char destination[24]{};
    struct Event { uint32_t address; int width; bool write; };
    std::vector<Event> events;
  } bus;
  constexpr uint32_t filteredSource = 0x40030000;
  constexpr uint32_t filteredDestination = 0x40040000;
  MemoryDevice filteredSourceDevice{filteredSource, filteredSource + 23, &bus,
      +[](void *context, uint32_t address, int width, uint32_t *value) {
        auto &state = *static_cast<FilteredMoveBus *>(context);
        state.events.push_back({address, width, false});
        *value = 0;
        for (int byte = 0; byte < width; ++byte)
          *value |= static_cast<uint32_t>(state.source[address - filteredSource + static_cast<uint32_t>(byte)])
                    << (static_cast<unsigned int>(byte) * BitsPerByte);
        return 0;
      }, nullptr};
  MemoryDevice filteredDestinationDevice{filteredDestination, filteredDestination + 23, &bus, nullptr,
      +[](void *context, uint32_t address, int width, uint32_t value) {
        auto &state = *static_cast<FilteredMoveBus *>(context);
        state.events.push_back({address, width, true});
        for (int byte = 0; byte < width; ++byte)
          state.destination[address - filteredDestination + static_cast<uint32_t>(byte)] =
              static_cast<unsigned char>(value >> (static_cast<unsigned int>(byte) * BitsPerByte));
        return 0;
      }};
  expect("register filtered 2D source", registerMemoryDevice(filteredSourceDevice) != 0, 0);
  expect("register filtered 2D destination", registerMemoryDevice(filteredDestinationDevice) != 0, 0);
  auto countEvents = [&bus](bool write, uint32_t address, int width) {
    uint32_t count = 0;
    for (const auto &event : bus.events)
      if (event.write == write && event.address == address && (width == 0 || event.width == width))
        ++count;
    return count;
  };
  for (int mode = 1; mode < 3; ++mode) {
    const unsigned char sourceBytes[] = {1, 0, 2, 3, 1, 2, 0, 4, 0, 0, 0, 0, 5, 6, 7, 8};
    std::copy(std::begin(sourceBytes), std::end(sourceBytes), bus.source);
    std::fill(std::begin(bus.destination), std::end(bus.destination), 0xaa);
    bus.events.clear();
    cpuState.areg = 1;
    cpuState.breg = 16;
    cpuState.creg = 16;
    move2dinit_(0);
    cpuState.areg = 16;
    cpuState.breg = filteredDestination;
    cpuState.creg = filteredSource;
    expect("filtered move2d MMIO succeeds", copies[mode](0) != 0, 0);
    expect("filtered move2d aligned word read", countEvents(false, filteredSource + 8, BytesPerWord), 1);
    expect("filtered move2d mixed word read", countEvents(false, filteredSource + 4, BytesPerWord), 1);
    for (uint32_t byte = 4; byte < 8; ++byte)
      expect("filtered move2d mixed word byte reread", countEvents(false, filteredSource + byte, 1), 1);
    expect("filtered move2d fully matching word read", countEvents(false, filteredSource + 12, BytesPerWord), 1);
    if (mode == 2) {
      expect("zero move2d full matching word write", countEvents(true, filteredDestination + 8, BytesPerWord), 1);
      expect("zero move2d skips nonzero word write", countEvents(true, filteredDestination + 12, 0), 0);
    } else {
      expect("nonzero move2d skips empty word write", countEvents(true, filteredDestination + 8, 0), 0);
      expect("nonzero move2d fully matching word write", countEvents(true, filteredDestination + 12, BytesPerWord), 1);
      expect("nonzero move2d optimized transaction count", bus.events.size(), 18);
    }
    if (mode == 2)
      expect("zero move2d optimized transaction count", bus.events.size(), 14);
    for (uint32_t index = 0; index < 24; ++index) {
      bool copied = index < 16 && (mode == 1 ? bus.source[index] != 0 : bus.source[index] == 0);
      expect("filtered move2d optimized data", bus.destination[index],
             copied ? bus.source[index] : 0xaa);
    }
  }
  std::fill(std::begin(bus.destination), std::end(bus.destination), 0xaa);
  bus.events.clear();
  cpuState.areg = 1;
  cpuState.breg = 16;
  cpuState.creg = 16;
  move2dinit_(0);
  cpuState.areg = 16;
  cpuState.breg = filteredDestination + 4;
  cpuState.creg = filteredSource;
  expect("misaligned filtered move2d succeeds", move2dnonzero_(0) != 0, 0);
  expect("misaligned filtered move2d byte transactions", bus.events.size(), 26);
  for (const auto &event : bus.events)
    expect("misaligned filtered move2d avoids word transaction", event.width == 1, 1);
  for (uint32_t index = 0; index < 24; ++index) {
    bool inCopySpan = index >= 4 && index < 20;
    uint32_t sourceIndex = inCopySpan ? index - 4 : 0;
    bool copied = inCopySpan && bus.source[sourceIndex] != 0;
    expect("misaligned filtered move2d data", bus.destination[index],
           copied ? bus.source[sourceIndex] : 0xaa);
  }
  expect("unregister filtered 2D source", unregisterMemoryDevice(filteredSource) != 0, 0);
  expect("unregister filtered 2D destination", unregisterMemoryDevice(filteredDestination) != 0, 0);

  cpuState.move2dBlockLength = 0;
  cpuState.areg = 4;
  cpuState.breg = 0x70006000;
  cpuState.creg = 0x70007000;
  expect("move2d zero rows does not access memory", move2dall_(0) != 0, 0);
  cpuState.move2dBlockLength = 0xffffffff;
  cpuState.areg = 0;
  cpuState.breg = 0x70006000;
  cpuState.creg = 0x70007000;
  expect("move2d zero width avoids row loop", move2dall_(0) != 0, 0);
  cpuState.move2dBlockLength = 2;
  cpuState.move2dSourceStride = 0xfffffffc;
  cpuState.move2dDestStride = 4;
  storeBytes(0x94004, 4, 0x11223344);
  storeBytes(0x94000, 4, 0x55667788);
  cpuState.areg = 4;
  cpuState.breg = 0x95000;
  cpuState.creg = 0x94004;
  move2dall_(0);
  uint32_t value = 0;
  readBytes(0x95000, 4, &value);
  expect("move2d negative source stride first row", value, 0x11223344);
  readBytes(0x95004, 4, &value);
  expect("move2d negative source stride second row", value, 0x55667788);
  cpuState.move2dBlockLength = 1;
  storeBytes(0xfffffffe, 1, 0x11);
  storeBytes(0xffffffff, 1, 0x22);
  storeBytes(0, 1, 0x33);
  storeBytes(1, 1, 0x44);
  cpuState.areg = 4;
  cpuState.breg = 0x96000;
  cpuState.creg = 0xfffffffe;
  move2dall_(0);
  readBytes(0x96000, 4, &value);
  expect("move2d source address wraps", value, 0x44332211);

  schedulerFixture(0x97000);
  cpuState.move2dBlockLength = 3;
  cpuState.move2dDestStride = 9;
  cpuState.move2dSourceStride = 7;
  storeBytes(0x98000 - 4, 4, 0x4000);
  enqueueProcess(0x98000, HIGH_PRIORITY);
  pollTimers();
  cpuState.areg = 1;
  cpuState.breg = 2;
  cpuState.creg = 3;
  move2dinit_(0);
  stopp_(0);
  expect("move2d parameters survive high-priority shadow", cpuState.move2dBlockLength, 3);
  expect("move2d destination stride survives shadow", cpuState.move2dDestStride, 9);
  expect("move2d source stride survives shadow", cpuState.move2dSourceStride, 7);
  schedulerFixture(0x20000);
}

static void testBlockedChannelSnapshot() {
  char directory[] = "/tmp/st20-channel-snapshot-XXXXXX";
  if (!mkdtemp(directory)) {
    expect("channel snapshot temporary directory", 0, 1);
    return;
  }
  schedulerFixture(0xa0000, HIGH_PRIORITY);
  storeBytes(0xa1000, 4, NO_PROCESS);
  cpuState.areg = 0xffffff80;
  cpuState.breg = 0xa1000;
  outbyte_(0);
  expect("channel snapshot sender blocked", cpuState.running, 0);
  expect("channel snapshot save", saveCPUState(directory) != 0, 0);
  resetScheduler();
  cpuState.wptr = 0;
  expect("channel snapshot load", loadCPUState(directory) != 0, 0);
  expect("channel snapshot retains blocked state", cpuState.running, 0);
  cpuState.running = true;
  cpuState.priority = LOW_PRIORITY;
  cpuState.wptr = 0xa3000;
  cpuState.areg = 1;
  cpuState.breg = 0xa1000;
  cpuState.creg = 0xa2000;
  expect("restored channel communication completes", in_(0) != 0, 0);
  uint32_t value = 0;
  readBytes(0xa2000, 1, &value);
  expect("restored scalar output data", value, 0x80);
  expect("restored sender priority", cpuState.priority, HIGH_PRIORITY);
  expect("restored sender resumes", cpuState.wptr, 0xa0000);
  std::string filename = std::string(directory) + "/cpu.bin";
  std::remove(filename.c_str());
  rmdir(directory);
  schedulerFixture(0x20000);
}

static void testTrapRecordsAndMasks() {
  for (unsigned char callerPriority : {static_cast<unsigned char>(HIGH_PRIORITY), static_cast<unsigned char>(LOW_PRIORITY)}) {
    for (unsigned char targetPriority : {static_cast<unsigned char>(HIGH_PRIORITY), static_cast<unsigned char>(LOW_PRIORITY)}) {
      for (bool encoded : {false, true}) {
        schedulerFixture(0xb0000, callerPriority);
        uint32_t priorMask = 0;
        expect("trapenb regression initializes selected mask", changeTrapMask(targetPriority, 1, true, &priorMask), 0);
        uint32_t controlBits = omrState.Enables & ~0x3fffU;
        cpuState.areg = 0x4006;
        cpuState.breg = targetPriority;
        cpuState.creg = 0x12345678;
        if (encoded) {
          cpuState.iptr = 0xb3000;
          storeBytes(cpuState.iptr, 2, 0xf760);
          expect("trapenb regression decodes", decodeNextInstr(), 0);
          expect("trapenb regression selects handler", instrEntry[instrCode].function == trapenb_, 1);
          int breakFlag = 0;
          expect("trapenb regression executes", execInstr(&breakFlag), 0);
          expect("trapenb regression advances Iptr", cpuState.iptr, 0xb3002);
        } else {
          expect("trapenb regression succeeds", trapenb_(0), 0);
        }
        expect("trapenb returns previous fourteen-bit mask", cpuState.areg, 1);
        expect("trapenb clears upper result bits including bit fifteen", cpuState.areg & ~0x3fffU, 0);
        expect("trapenb leaves B undefined", cpuState.breg, undefinedWord);
        expect("trapenb leaves C undefined", cpuState.creg, undefinedWord);
        expect("trapenb ignores reserved input bits", trapEnableMask(targetPriority), 7);
        unsigned char otherPriority = targetPriority == HIGH_PRIORITY ? LOW_PRIORITY : HIGH_PRIORITY;
        expect("trapenb preserves other priority mask", trapEnableMask(otherPriority), 0);
        expect("trapenb preserves global and reserved control bits", omrState.Enables & ~0x3fffU, controlBits);
      }
    }
  }
  schedulerFixture(0xb0000);
  uint32_t previous = 0;
  changeTrapMask(HIGH_PRIORITY, 0xffffffff, true, &previous);
  expect("high-priority trap masks independent", trapEnableMask(HIGH_PRIORITY), 0x3fff);
  expect("low-priority trap masks unchanged", trapEnableMask(LOW_PRIORITY), 0);
  changeTrapMask(LOW_PRIORITY, INTEGER_OVF_TRAPENB, true, &previous);
  expect("active-priority mask reflected in enables", static_cast<uint32_t>(omrState.Enables) & 0x3fff, INTEGER_OVF_TRAPENB);
  changeTrapMask(HIGH_PRIORITY, 0x3fff, false, &previous);
  expect("trap disable returns previous priority mask", previous, 0x3fff);
  expect("trap disable preserves other priority", trapEnableMask(LOW_PRIORITY), INTEGER_OVF_TRAPENB);
  expect("high breakpoint handler address", trapRecordAddress(HIGH_PRIORITY, 0, false), 0x80000040);
  expect("high breakpoint trapped address", trapRecordAddress(HIGH_PRIORITY, 0, true), 0x80000050);
  expect("low scheduler handler address", trapRecordAddress(LOW_PRIORITY, 3, false), 0x80000120);
  for (uint32_t group = 0; group < 4; ++group) {
    for (uint32_t priority = 0; priority < 2; ++priority) {
      for (uint32_t index = 0; index < 4; ++index)
        storeBytes(0xb1000 + index * 4, 4, static_cast<uint32_t>(0x12340000U + group * 16 + priority * 4 + index));
      cpuState.areg = group;
      cpuState.breg = 0xb1000;
      cpuState.creg = priority;
      expect("ldtraph transfers full descriptor", ldtraph_(0) != 0, 0);
      cpuState.areg = group;
      cpuState.breg = 0xb2000;
      cpuState.creg = priority;
      expect("sttraph exports descriptor", sttraph_(0) != 0, 0);
      for (uint32_t index = 0; index < 4; ++index) {
        uint32_t value = 0;
        readBytes(0xb2000 + index * 4, 4, &value);
        expect("trap descriptor word", value, 0x12340000U + group * 16 + priority * 4 + index);
      }
    }
  }
  schedulerFixture(0x20000);
}

static void installTrap(unsigned char priority, uint32_t group, uint32_t workspace, uint32_t iptr,
                        uint32_t enables = 0xffffc000U) {
  uint32_t address = trapRecordAddress(priority, group, false);
  storeBytes(address, 4, static_cast<uint32_t>(enables));
  storeBytes(address + 4, 4, static_cast<uint32_t>(group << 18));
  storeBytes(address + 8, 4, static_cast<uint32_t>(workspace));
  storeBytes(address + 12, 4, static_cast<uint32_t>(iptr));
}

static void testShadowRegisters() {
  schedulerFixture(0x18f000, HIGH_PRIORITY);
  storeBytes(0x18f204, 4, STATUS_TIMESLICE_ENABLE);
  storeBytes(0x18f208, 4, NO_PROCESS);
  storeBytes(0x18f20c, 4, undefinedWord);
  cpuState.areg = 2;
  cpuState.breg = LOW_PRIORITY;
  cpuState.creg = 0x18f200;
  expect("inactive native shadow control load", ldshadow_(0) != 0, 0);
  cpuState.areg = 2;
  cpuState.breg = LOW_PRIORITY;
  cpuState.creg = 0x18f100;
  expect("inactive native shadow status export", stshadow_(0) != 0, 0);
  uint32_t inactiveStatus = 0;
  readBytes(0x18f104, 4, &inactiveStatus);
  expect("inactive native shadow remains invalid", inactiveStatus & STATUS_VALID, 0);
  expect("inactive native shadow retains timeslice state", inactiveStatus & STATUS_TIMESLICE_ENABLE, STATUS_TIMESLICE_ENABLE);
  auto preemptLow = [] {
    schedulerFixture(0x190000);
    cpuState.areg = 0x1111;
    cpuState.breg = 0x2222;
    cpuState.creg = 0x3333;
    cpuState.status = STATUS_TIMESLICE_ENABLE | 0x40U;
    uint32_t previous = 0;
    changeTrapMask(LOW_PRIORITY, 6, true, &previous);
    changeTrapMask(HIGH_PRIORITY, 8, true, &previous);
    omrState.Enables = (omrState.Enables & ~0x00ff0000U) | 0x00a50000U;
    storeBytes(0x191000 - 4, 4, 0x9a00);
    enqueueProcess(0x191000, HIGH_PRIORITY);
    expect("shadow fixture preempts low", pollTimers() != 0, 0);
    expect("shadow fixture high process", cpuState.wptr, 0x191000);
  };
  preemptLow();
  setPortableInstructionAbi(true);
  storeBytes(0x18f300, 4, 0xa500c000);
  cpuState.areg = 1;
  cpuState.breg = LOW_PRIORITY;
  cpuState.creg = 0x18f300;
  expect("portable enable export succeeds", stshadow_(0) != 0, 0);
  uint32_t portableEnables = 0;
  readBytes(0x18f300, 4, &portableEnables);
  expect("portable enable export preserves reserved bits", portableEnables, 0xa5a5c006);
  storeBytes(0x18f300, 4, 0x12341234);
  cpuState.areg = 1;
  cpuState.breg = LOW_PRIORITY;
  cpuState.creg = 0x18f300;
  expect("portable enable import succeeds", ldshadow_(0) != 0, 0);
  expect("portable import preserves upper global byte", omrState.Enables >> 24, 0xff);
  expect("portable import replaces active global byte", (omrState.Enables >> 16) & 0xffU, 0x34);
  setPortableInstructionAbi(false);
  for (uint32_t selection = 0; selection < 16; ++selection) {
    preemptLow();
    const uint32_t seed = 0xa500c000;
    for (uint32_t index = 0; index < 12; ++index) storeBytes(0x194000 + index * 4U, 4, seed + index);
    cpuState.areg = selection;
    cpuState.breg = LOW_PRIORITY;
    cpuState.creg = 0x194000;
    expect("selective stshadow succeeds", stshadow_(0) != 0, 0);
    const uint32_t expected[] = {0xffa50006U,
        STATUS_VALID | STATUS_TIMESLICE_ENABLE | 0x40U, 0x190000, 0x3000, 0x1111, 0x2222, 0x3333};
    for (uint32_t index = 0; index < 12; ++index) {
            bool selected = (selection & (index == 0 ? 1U : index <= 3 ? 2U : index <= 6 ? 4U : 8U)) != 0;
      uint32_t value = 0;
      readBytes(0x194000 + index * 4U, 4, &value);
            expect("stshadow only writes selected words", value,
              selected ? index < 7 ? expected[index] : undefinedWord : seed + index);
    }
    expect("stshadow A undefined", cpuState.areg, undefinedWord);
    expect("stshadow B undefined", cpuState.breg, undefinedWord);
    expect("stshadow C undefined", cpuState.creg, undefinedWord);
    expect("stshadow preserves active high mask", trapEnableMask(HIGH_PRIORITY), 8);
    expect("stshadow preserves low mask", trapEnableMask(LOW_PRIORITY), 6);

    preemptLow();
    const uint32_t record[] = {0x12341234, STATUS_VALID | STATUS_TIMESLICE_ENABLE | 2U,
                              0x192000, 0x9b00, 0xabc, 0xdef, 0x123};
    for (uint32_t index = 0; index < 7; ++index) storeBytes(0x195000 + index * 4U, 4, record[index]);
    for (uint32_t index = 7; index < 12; ++index) storeBytes(0x195000 + index * 4U, 4, seed + index);
    cpuState.areg = selection;
    cpuState.breg = LOW_PRIORITY;
    cpuState.creg = 0x195000;
    expect("selective ldshadow succeeds", ldshadow_(0) != 0, 0);
    expect("ldshadow preserves active workspace", cpuState.wptr, 0x191000);
    expect("ldshadow preserves active mask", trapEnableMask(HIGH_PRIORITY), 8);
    expect("ldshadow selected low mask", trapEnableMask(LOW_PRIORITY), selection & 1U ? 0x1234 : 6);
    expect("ldshadow selected full global field", omrState.Enables >> 16, selection & 1U ? 0x1234 : 0xffa5);
    expect("ldshadow A undefined", cpuState.areg, undefinedWord);
    expect("ldshadow caller deschedules", stopp_(0) != 0, 0);
    expect("ldshadow restored low workspace", cpuState.wptr, selection & 2U ? 0x192000 : 0x190000);
    expect("ldshadow restored low entry", cpuState.iptr, selection & 2U ? 0x9b00 : 0x3000);
    expect("ldshadow restored low A", cpuState.areg, selection & 4U ? 0xabc : 0x1111);
    expect("ldshadow restored low B", cpuState.breg, selection & 4U ? 0xdef : 0x2222);
    expect("ldshadow restored low C", cpuState.creg, selection & 4U ? 0x123 : 0x3333);
  }
  preemptLow();
  storeBytes(0xfffffff8U, 4, 0);
  cpuState.areg = 7;
  cpuState.breg = LOW_PRIORITY;
  cpuState.creg = 0xfffffff8U;
  cpuState.iptr = 0x9c00;
  storeBytes(0x9c00, 2, 0xfd60);
  expect("encoded wrapping stshadow decode", decodeNextInstr() != 0, 0);
  int watchFlag = 0;
  expect("encoded wrapping stshadow executes", execInstr(&watchFlag) != 0, 0);
  uint32_t value = 0;
  readBytes(0, 4, &value);
  expect("stshadow record wraps at 32 bits", value, 0x190000);
  cpuState.areg = 7;
  cpuState.breg = LOW_PRIORITY;
  cpuState.creg = 0xfffffff8U;
  cpuState.iptr = 0x9c00;
  storeBytes(0x9c00, 2, 0xfc60);
  expect("encoded wrapping ldshadow decode", decodeNextInstr() != 0, 0);
  expect("encoded wrapping ldshadow executes", execInstr(&watchFlag) != 0, 0);
  stopp_(0);
  expect("wrapping shadow roundtrip resumes low", cpuState.wptr, 0x190000);
  expect("wrapping shadow roundtrip A", cpuState.areg, 0x1111);

  schedulerFixture(0x193000, HIGH_PRIORITY);
  storeBytes(0x196010, 4, 0x987);
  storeBytes(0x196014, 4, 0x654);
  storeBytes(0x196018, 4, 0x321);
  cpuState.areg = 4;
  cpuState.breg = LOW_PRIORITY;
  cpuState.creg = 0x196000;
  expect("ldshadow can stage inactive low stack", ldshadow_(0) != 0, 0);
  storeBytes(0x196004, 4, STATUS_VALID | STATUS_TIMESLICE_ENABLE);
  storeBytes(0x196008, 4, 0x197000);
  storeBytes(0x19600c, 4, 0x9d00);
  cpuState.areg = 2;
  cpuState.breg = LOW_PRIORITY;
  cpuState.creg = 0x196000;
  expect("ldshadow installs low context", ldshadow_(0) != 0, 0);
  stopp_(0);
  expect("staged low context runs", cpuState.wptr, 0x197000);
  expect("staged low A retained", cpuState.areg, 0x987);
  expect("staged low B retained", cpuState.breg, 0x654);
  expect("staged low C retained", cpuState.creg, 0x321);

  preemptLow();
  uint32_t previousEnables = omrState.Enables;
  storeBytes(0x6c000000, 4, 0x005a1234);
  storeBytes(0x6c000004, 4, STATUS_VALID);
  cpuState.areg = 3;
  cpuState.breg = LOW_PRIORITY;
  cpuState.creg = 0x6c000000;
  expect("ldshadow missing selected word fails", ldshadow_(0) == READ_UNUSED_MEM, 1);
  expect("failed ldshadow preserves enables", omrState.Enables, previousEnables);
  expect("failed ldshadow preserves selector", cpuState.areg, 3);
  expect("failed ldshadow preserves low trap mask", trapEnableMask(LOW_PRIORITY), 6);
  storeBytes(0x6c000008, 4, 0x197001);
  storeBytes(0x6c00000c, 4, 0x9d00);
  expect("ldshadow rejects active unaligned workspace", ldshadow_(0) == INVALID_SHADOW_ACCESS, 1);
  storeBytes(0x6c000004, 4, STATUS_VALID | (1U << 26));
  expect("ldshadow rejects unsupported restart", ldshadow_(0) == INTERRUPTED_OPERATION_UNSUPPORTED, 1);
  expect("invalid ldshadow preserves global field", omrState.Enables, previousEnables);
  for (uint32_t priority : {2U, 256U, 0xffffffffU}) {
    cpuState.areg = 1;
    cpuState.breg = priority;
    expect("ldshadow rejects invalid priority", ldshadow_(0) == INVALID_SHADOW_ACCESS, 1);
    expect("stshadow rejects invalid priority", stshadow_(0) == INVALID_SHADOW_ACCESS, 1);
  }
  cpuState.areg = 8;
  cpuState.breg = LOW_PRIORITY;
  expect("ldshadow requires all selected block words", ldshadow_(0) == READ_UNUSED_MEM, 1);
  expect("failed block load preserves selector", cpuState.areg, 8);
  expect("stshadow exports idle native block group", stshadow_(0) != 0, 0);
  cpuState.areg = 8;
  cpuState.breg = HIGH_PRIORITY;
  cpuState.status |= 1U << 26;
  expect("stshadow rejects interrupted native block state", stshadow_(0) == INTERRUPTED_OPERATION_UNSUPPORTED, 1);
  expect("ldshadow rejects interrupted native block state", ldshadow_(0) == INTERRUPTED_OPERATION_UNSUPPORTED, 1);
  cpuState.status &= ~STATUS_INTERRUPTED_OPERATION_MASK;
  cpuState.areg = 1;
  cpuState.breg = HIGH_PRIORITY;
  expect("ldshadow rejects loading current bank", ldshadow_(0) == INVALID_SHADOW_ACCESS, 1);
  cpuState.areg = 0;
  cpuState.breg = LOW_PRIORITY;
  cpuState.creg = 0x6c010000;
  expect("empty ldshadow does not access memory", ldshadow_(0) != 0, 0);
  cpuState.areg = 0;
  cpuState.breg = LOW_PRIORITY;
  cpuState.creg = 0x6c010000;
  expect("empty stshadow does not access memory", stshadow_(0) != 0, 0);

  MemoryDevice failing{0x198000, 0x19802f, nullptr, nullptr,
      +[](void *, uint32_t, int, uint32_t) { return WRITE_FAILURE; }};
  registerMemoryDevice(failing);
  cpuState.areg = 2;
  cpuState.breg = LOW_PRIORITY;
  cpuState.creg = failing.start;
  expect("stshadow propagates device write failure", stshadow_(0) == WRITE_FAILURE, 1);
  expect("failed stshadow preserves selector", cpuState.areg, 2);
  unregisterMemoryDevice(failing.start);
  schedulerFixture(0x193000, HIGH_PRIORITY);
  cpuState.areg = 6;
  cpuState.breg = HIGH_PRIORITY;
  cpuState.creg = 0x199000;
  expect("stshadow can store current bank", stshadow_(0) != 0, 0);
  readBytes(0x199010, 4, &value);
  expect("current stshadow uses original A", value, 6);
  readBytes(0x199018, 4, &value);
  expect("current stshadow uses original C", value, 0x199000);
  preemptLow();
  storeBytes(0x199004, 4, 0);
  storeBytes(0x199008, 4, NO_PROCESS);
  storeBytes(0x19900c, 4, 0);
  cpuState.areg = 2;
  cpuState.breg = LOW_PRIORITY;
  cpuState.creg = 0x199000;
  expect("ldshadow can invalidate low context", ldshadow_(0) != 0, 0);
  cpuState.areg = 6;
  cpuState.breg = LOW_PRIORITY;
  cpuState.creg = 0x199000;
  expect("stshadow reports absent low context", stshadow_(0) != 0, 0);
  readBytes(0x199004, 4, &value);
  expect("absent shadow has invalid Status", value & STATUS_VALID, 0);
  readBytes(0x199008, 4, &value);
  expect("absent shadow uses no-process workspace", value, NO_PROCESS);
  stopp_(0);
  expect("invalidated low context is not resumed", cpuState.running, 0);

  schedulerFixture(0x193000);
  uint32_t previous = 0;
  changeTrapMask(LOW_PRIORITY, 6, true, &previous);
  storeBytes(0x199000, 4, 0xaa571234);
  cpuState.areg = 1;
  cpuState.breg = HIGH_PRIORITY;
  cpuState.creg = 0x199000;
  expect("ldshadow loads inactive high enables", ldshadow_(0) != 0, 0);
  expect("inactive high trap mask loaded", trapEnableMask(HIGH_PRIORITY), 0x1234);
  expect("inactive high load preserves active low mask", trapEnableMask(LOW_PRIORITY), 6);
  expect("inactive high load global field", (omrState.Enables >> 16) & 255U, 0x57);
  cpuState.areg = 1;
  cpuState.breg = HIGH_PRIORITY;
  cpuState.creg = 0x199000;
  expect("stshadow stores inactive high enables", stshadow_(0) != 0, 0);
  readBytes(0x199000, 4, &value);
  expect("inactive high enable roundtrip", value, 0xaa571234);
  cpuState.areg = 6;
  cpuState.breg = HIGH_PRIORITY;
  cpuState.creg = 0x199000;
  expect("stshadow rejects high state from low", stshadow_(0) == INVALID_SHADOW_ACCESS, 1);

  preemptLow();
  installTrap(HIGH_PRIORITY, 0, 0x19a000, 0x9e00);
  omrState.Enables |= BREAKPOINT_TRAPENB;
  requestTrap(BREAKPOINT_TRAPENB, HIGH_PRIORITY);
  const uint32_t trapInput[] = {0, STATUS_VALID | STATUS_TIMESLICE_ENABLE,
      0x191000, 0x9d80, 0x4444, 0x5555, 0x6666};
  for (uint32_t index = 0; index < 7; ++index)
    storeBytes(0x199000 + index * 4U, 4, trapInput[index]);
  cpuState.areg = 6;
  cpuState.breg = LOW_PRIORITY;
  cpuState.creg = 0x199000;
  expect("native HP trap loads independent low state", ldshadow_(0) != 0, 0);
  expect("native HP trap remains active after low load", trapHandlerActive(), 1);
  cpuState.areg = 6;
  cpuState.breg = LOW_PRIORITY;
  cpuState.creg = 0x199100;
  expect("native HP trap stores independent low state", stshadow_(0) != 0, 0);
  for (uint32_t index = 1; index < 7; ++index) {
    readBytes(0x199100 + index * 4U, 4, &value);
    expect("native trap shadow roundtrip value", value, trapInput[index]);
  }
  char trapDirectory[] = "/tmp/st20-native-trap-shadow-XXXXXX";
  expect("native trap shadow snapshot directory", mkdtemp(trapDirectory) != nullptr, 1);
  expect("native trap shadow snapshot saves", saveCPUState(trapDirectory) != 0, 0);
  resetScheduler();
  expect("native trap shadow snapshot restores", loadCPUState(trapDirectory) != 0, 0);
  expect("native trap shadow snapshot retains handler", trapHandlerActive(), 1);
  expect("native low edit leaves HP trap returnable", tret_(0) != 0, 0);
  stopp_(0);
  expect("native trap low edit resumes workspace", cpuState.wptr, 0x191000);
  expect("native trap low edit resumes Iptr", cpuState.iptr, 0x9d80);
  expect("native trap low edit resumes A", cpuState.areg, 0x4444);
  expect("native trap low edit resumes B", cpuState.breg, 0x5555);
  expect("native trap low edit resumes C", cpuState.creg, 0x6666);
  std::string trapFilename = std::string(trapDirectory) + "/cpu.bin";
  unlink(trapFilename.c_str());
  rmdir(trapDirectory);

  schedulerFixture(0x19d000, LOW_PRIORITY);
  installTrap(HIGH_PRIORITY, TRAP_GROUP_SCHEDULER, 0x19a000, 0x9e00);
  changeTrapMask(HIGH_PRIORITY, RUN_TRAPENB, true, &previous);
  cpuState.areg = 0x19e000;
  cpuState.breg = 0x1234;
  cpuState.creg = 0x5678;
  expect("native cross-priority runp trap enters HP", runp_(0) != 0, 0);
  expect("native cross-priority trap priority", cpuState.priority, HIGH_PRIORITY);
  cpuState.areg = 6;
  cpuState.breg = LOW_PRIORITY;
  cpuState.creg = 0x199300;
  expect("native captured low trap bank export", stshadow_(0) != 0, 0);
  readBytes(0x199308, 4, &value);
  expect("native captured export caller workspace", value, 0x19d000);
  readBytes(0x199310, 4, &value);
  expect("native runp captures original A", value, 0x19e000);
  readBytes(0x199314, 4, &value);
  expect("native runp captures original B", value, 0x1234);
  readBytes(0x199318, 4, &value);
  expect("native runp captures original C", value, 0x5678);
  cpuState.areg = 6;
  cpuState.breg = LOW_PRIORITY;
  cpuState.creg = 0x199000;
  expect("native captured bank edits valid control and stack", ldshadow_(0) != 0, 0);
  storeBytes(0x199704, 4, 0);
  storeBytes(0x199708, 4, 0x191000);
  storeBytes(0x19970c, 4, 0x9d80);
  cpuState.areg = 2;
  cpuState.breg = LOW_PRIORITY;
  cpuState.creg = 0x199700;
  expect("native captured bank invalidates", ldshadow_(0) != 0, 0);
  cpuState.areg = 6;
  cpuState.breg = LOW_PRIORITY;
  cpuState.creg = 0x199800;
  expect("native captured invalid bank exports", stshadow_(0) != 0, 0);
  readBytes(0x199804, 4, &value);
  expect("native captured invalid export clears valid bit", value, 0);
  readBytes(0x199808, 4, &value);
  expect("native captured invalid export retains workspace", value, 0x191000);
  readBytes(0x199810, 4, &value);
  expect("native captured invalid export retains A", value, 0x4444);
  char invalidDirectory[] = "/tmp/st20-invalid-captured-bank-XXXXXX";
  expect("native invalid captured snapshot directory", mkdtemp(invalidDirectory) != nullptr, 1);
  expect("native invalid captured snapshot saves", saveCPUState(invalidDirectory) != 0, 0);
  resetScheduler();
  expect("native invalid captured snapshot restores", loadCPUState(invalidDirectory) != 0, 0);
  expect("native invalid captured snapshot keeps handler active", trapHandlerActive(), 1);
  storeBytes(0x199704, 4, STATUS_VALID | STATUS_TIMESLICE_ENABLE);
  cpuState.areg = 2;
  cpuState.breg = LOW_PRIORITY;
  cpuState.creg = 0x199700;
  expect("native captured bank reactivates", ldshadow_(0) != 0, 0);
  std::string invalidFilename = std::string(invalidDirectory) + "/cpu.bin";
  unlink(invalidFilename.c_str());
  rmdir(invalidDirectory);
  uint32_t trapped = trapRecordAddress(HIGH_PRIORITY, TRAP_GROUP_SCHEDULER, true);
  uint32_t savedRecord[4]{};
  for (uint32_t index = 0; index < 4; ++index) readBytes(trapped + index * 4, 4, &savedRecord[index]);
  storeBytes(0x199400, 4, 0x005a0022);
  storeBytes(0x199410, 4, 0x77);
  storeBytes(0x199414, 4, 0x88);
  storeBytes(0x199418, 4, 0x99);
  cpuState.areg = 5;
  cpuState.breg = LOW_PRIORITY;
  cpuState.creg = 0x199400;
  expect("native captured bank loads enables and stack", ldshadow_(0) != 0, 0);
  for (uint32_t index = 0; index < 4; ++index) {
    readBytes(trapped + index * 4, 4, &value);
    expect("native captured load leaves trapped control record unchanged", value, savedRecord[index]);
  }
  char capturedDirectory[] = "/tmp/st20-native-captured-shadow-XXXXXX";
  expect("native captured bank snapshot directory", mkdtemp(capturedDirectory) != nullptr, 1);
  expect("native captured bank snapshot saves", saveCPUState(capturedDirectory) != 0, 0);
  resetScheduler();
  expect("native captured bank snapshot restores", loadCPUState(capturedDirectory) != 0, 0);
  expect("native captured snapshot preserves active handler", trapHandlerActive(), 1);
  MemoryDevice unusedControl{trapped + 8, trapped + 15, nullptr,
      +[](void *, uint32_t, int, uint32_t *) { return READ_UNUSED_MEM; }, nullptr};
  expect("native captured unused control slots register", registerMemoryDevice(unusedControl) != 0, 0);
  expect("native captured bank trap returns", tret_(0) != 0, 0);
  expect("native captured unused control slots unregister", unregisterMemoryDevice(unusedControl.start) != 0, 0);
  expect("native captured bank edited workspace resumes", cpuState.wptr, 0x191000);
  expect("native captured bank edited Iptr resumes", cpuState.iptr, 0x9d80);
  expect("native captured bank edited Status resumes", cpuState.status, STATUS_VALID | STATUS_TIMESLICE_ENABLE);
  expect("native captured bank caller priority preserved", cpuState.priority, LOW_PRIORITY);
  expect("native captured bank edited A resumes", cpuState.areg, 0x77);
  expect("native captured bank edited B resumes", cpuState.breg, 0x88);
  expect("native captured bank edited C resumes", cpuState.creg, 0x99);
  expect("native captured bank restores original global enables", omrState.Enables >> 16, savedRecord[0] >> 16);
  expect("native captured bank retains edited low trap enables", trapEnableMask(LOW_PRIORITY), 0x22);
  std::string capturedFilename = std::string(capturedDirectory) + "/cpu.bin";
  unlink(capturedFilename.c_str());
  rmdir(capturedDirectory);

  preemptLow();
  const uint32_t edited[] = {0x005a1234, STATUS_VALID | STATUS_TIMESLICE_ENABLE,
                            0x19c000, 0x9f00, 0x7777, 0x8888, 0x9999};
  for (uint32_t index = 0; index < 7; ++index) storeBytes(0x19b000 + index * 4U, 4, edited[index]);
  cpuState.areg = 7;
  cpuState.breg = LOW_PRIORITY;
  cpuState.creg = 0x19b000;
  expect("ldshadow edits bank before snapshot", ldshadow_(0) != 0, 0);
  char directory[] = "/tmp/st20-shadow-XXXXXX";
  expect("shadow snapshot directory", mkdtemp(directory) != nullptr, 1);
  expect("save edited shadow snapshot", saveCPUState(directory) != 0, 0);
  resetScheduler();
  expect("load edited shadow snapshot", loadCPUState(directory) != 0, 0);
  expect("edited shadow resumes after snapshot", stopp_(0) != 0, 0);
  expect("snapshot keeps edited shadow workspace", cpuState.wptr, 0x19c000);
  expect("snapshot keeps edited shadow Iptr", cpuState.iptr, 0x9f00);
  expect("snapshot keeps edited shadow A", cpuState.areg, 0x7777);
  expect("snapshot keeps edited shadow B", cpuState.breg, 0x8888);
  expect("snapshot keeps edited shadow C", cpuState.creg, 0x9999);
  expect("snapshot keeps edited low trap mask", trapEnableMask(LOW_PRIORITY), 0x1234);
  std::string filename = std::string(directory) + "/cpu.bin";
  unlink(filename.c_str());
  rmdir(directory);
  schedulerFixture(0xc04307b8);
  installTrap(LOW_PRIORITY, TRAP_GROUP_SCHEDULER, 0xc042a5e0, 0xc042a004);
  omrState.Enables |= SIGNAL_TRAPENB;
  expect("native block save enters firmware scheduler", requestTrap(SIGNAL_TRAPENB, LOW_PRIORITY) != 0, 0);
  cpuState.areg = 8;
  cpuState.breg = LOW_PRIORITY;
  cpuState.creg = 0xc042a2e8;
  cpuState.iptr = 0xc042a1bd;
  storeBytes(cpuState.iptr, 2, 0xfd60);
  expect("firmware block stshadow decode", decodeNextInstr() != 0, 0);
  expect("firmware block stshadow executes", execInstr(&watchFlag) != 0, 0);
  for (uint32_t index = 7; index < 12; ++index) {
    expect("firmware block save readable", readBytes(0xc042a2e8 + index * 4U, 4, &value) != 0, 0);
    expect("firmware idle block save undefined", value, undefinedWord);
  }
  expect("firmware block save preserves active trap", trapHandlerActive(), 1);
  expect("firmware block save trap returns", tret_(0) != 0, 0);
  for (unsigned char priority : {static_cast<unsigned char>(HIGH_PRIORITY),
                                 static_cast<unsigned char>(LOW_PRIORITY)}) {
    schedulerFixture(0x19d000, priority);
    const uint32_t record[] = {0x005a1234, STATUS_VALID | STATUS_TIMESLICE_ENABLE,
                              0x19e000, 0x9f00, 0x1111, 0x2222, 0x3333,
                              undefinedWord, undefinedWord, undefinedWord, undefinedWord, undefinedWord};
    for (uint32_t index = 0; index < 12; ++index) storeBytes(0x19f000 + index * 4U, 4, record[index]);
    installTrap(priority, TRAP_GROUP_SCHEDULER, 0x19d800, 0x9e00);
    omrState.Enables |= SIGNAL_TRAPENB;
    expect("native restart enters scheduler", requestTrap(SIGNAL_TRAPENB, priority) != 0, 0);
    storeBytes(0x19f004, 4, STATUS_VALID | (1U << 26));
    cpuState.areg = 0x19f000;
    expect("native restart rejects interrupted operation", restart_(0) == INTERRUPTED_OPERATION_UNSUPPORTED, 1);
    expect("rejected native restart retains trap", trapHandlerActive(), 1);
    expect("rejected native restart retains argument", cpuState.areg, 0x19f000);
    storeBytes(0x19f004, 4, record[1]);
    storeBytes(0x19f008, 4, 0x19e001);
    expect("native restart rejects unaligned workspace", restart_(0) == INVALID_PROCESS_LIST, 1);
    storeBytes(0x19f008, 4, record[2]);
    cpuState.iptr = 0x9e00;
    storeBytes(cpuState.iptr, 2, 0xfe62);
    expect("encoded native restart decode", decodeNextInstr() != 0, 0);
    expect("encoded native restart executes", execInstr(&watchFlag) != 0, 0);
    expect("native restart consumes scheduler trap", trapHandlerActive(), 0);
    expect("native restart restores workspace", cpuState.wptr, record[2]);
    expect("native restart restores instruction", cpuState.iptr, record[3]);
    expect("native restart restores Status", cpuState.status, record[1]);
    expect("native restart restores A", cpuState.areg, record[4]);
    expect("native restart restores B", cpuState.breg, record[5]);
    expect("native restart restores C", cpuState.creg, record[6]);
    expect("native restart restores enables", omrState.Enables & 0x00ff3fffU, record[0]);
    expect("native restart preserves priority", cpuState.priority, priority);
    expect("native restart starts process", cpuState.running, 1);
  }
  schedulerFixture(0x20000);
}

static void testSti5512Devices() {
  schedulerFixture(0x180000);
  expect("initialize STi5512 devices", initSti5512Devices() != 0, 0);
  expect("STi5512 revision mapped", isMemoryDeviceAddress(0x78), 1);
  expect("STi5512 does not install ASC2", isMemoryDeviceAddress(0x20005000), 0);
  uint32_t value = 0xffffffffU;
  expect("STi5512 revision byte readable", readBytes(0x78, 1, &value) != 0, 0);
  expect("STi5512 synthetic default revision", value, 0);
  expect("STi5512 revision configurable", initSti5512Devices(0xaa) != 0, 0);
  reset(0x78, 0x11223344, 0x55667788);
  expect("STi5512 devlb succeeds", devlb_(0) != 0, 0);
  expect("STi5512 devlb revision", cpuState.areg, 0xaa);
  expect("STi5512 devlb preserves B", cpuState.breg, 0x11223344);
  expect("STi5512 devlb preserves C", cpuState.creg, 0x55667788);
  expect("STi5512 revision rejects writes", storeBytes(0x78, 1, 0x55) == DEVICE_ACCESS_UNSUPPORTED, 1);
  expect("STi5512 revision rejects wide reads", readBytes(0x78, 4, &value) == INVALID_DEVICE_ACCESS, 1);
  readBytes(0x78, 1, &value);
  expect("STi5512 rejected write preserves revision", value, 0xaa);
  shutdownSti5512Devices();
  expect("STi5512 shutdown removes revision", isMemoryDeviceAddress(0x78), 0);
  expect("STi5512 can initialize again", initSti5512Devices(0xff) != 0, 0);
  readBytes(0x78, 1, &value);
  expect("STi5512 maximum revision byte", value, 0xff);
  shutdownSti5512Devices();
  schedulerFixture(0x20000);
}

static void testSti5518Devices() {
  schedulerFixture(0x180000);
  setPortableInstructionAbi(false);
  expect("initialize native chip devices", initSti5518Devices() != 0, 0);
  uint32_t value = 0xffffffffU;
  expect("native ASC RX readable", readBytes(0x20005008, 4, &value) != 0, 0);
  expect("native ASC RX reset value", value, 0);
  storeBytes(0x184018, 4, 0x80000000);
  storeBytes(0x18401c, 4, 0x9400);
  storeBytes(0x20000018, 4, 0x184021);
  storeBytes(0x20000058, 4, 1);
  storeBytes(0x20011020, 4, 6);
  storeBytes(0x2000500c, 4, 0x1189);
  storeBytes(0x20005010, 4, 4);
  omrState.Enables |= LP_EXTERNALEVENT_ENB;
  expect("masked ASC IRQ poll", pollSti5518Devices() != 0, 0);
  expect("controller mask defers ASC IRQ", interruptHandlerActive(), 0);
  storeBytes(0x200000c4, 4, 0x40);
  omrState.Enables &= ~static_cast<uint32_t>(LP_EXTERNALEVENT_ENB);
  expect("globally masked ASC IRQ poll", pollSti5518Devices() != 0, 0);
  expect("CPU mask defers ASC IRQ", interruptHandlerActive(), 0);
  omrState.Enables |= LP_EXTERNALEVENT_ENB;
  storeBytes(0x200111b4, 4, 1U << 8);
  readBytes(0x200111bc, 4, &value);
  expect("ASC source mask clears source eight", value & (1U << 8), 0);
  expect("source masked ASC IRQ poll", pollSti5518Devices() != 0, 0);
  expect("source mask defers ASC IRQ", interruptHandlerActive(), 0);
  storeBytes(0x200111b8, 4, 1U << 8);
  expect("ASC TX ready enters native IRQ", pollSti5518Devices() != 0, 0);
  expect("ASC IRQ active", activeExternalInterruptLevel() == 6, 1);
  expect("ASC IRQ native workspace", cpuState.wptr, 0x184000);
  readBytes(0x20000100, 4, &value);
  expect("controller executing register", value, 0x40);
  storeBytes(0x20005010, 4, 0);
  expect("ASC IRQ return", iret_(0) != 0, 0);
  readBytes(0x20000100, 4, &value);
  expect("iret acknowledges controller", value, 0);
  storeBytes(0x20005004, 4, 0x48);
  storeBytes(0x20005004, 4, 0x69);
  auto bytes = takeSti5518Asc2Transmit();
  expect("ASC transmit captures bytes", bytes.size(), 2);
  if (bytes.size() == 2) {
    expect("ASC first transmitted byte", bytes[0], 0x48);
    expect("ASC second transmitted byte", bytes[1], 0x69);
  }
  pollSti5518Devices();
  pollSti5518Devices();
  readBytes(0x20005014, 4, &value);
  expect("ASC transmitter returns to empty", value & 6U, 6);
  expect("ASC RX rejects writes", storeBytes(0x20005008, 4, 1) == DEVICE_ACCESS_UNSUPPORTED, 1);
  expect("ASC status rejects writes", storeBytes(0x20005014, 4, 1) == DEVICE_ACCESS_UNSUPPORTED, 1);
  expect("ASC rejects unsupported byte access", readBytes(0x20005014, 1, &value) == INVALID_DEVICE_ACCESS, 1);
  storeBytes(0x200000c8, 4, 0x10000);
  readBytes(0x200000c0, 4, &value);
  expect("controller clear all mask", value, 0);
  expect("reset both native ASC ports", initSti5518Devices() != 0, 0);
  expect("native ASC1 RX readable", readBytes(0x20004008, 4, &value) != 0, 0);
  expect("native ASC1 RX reset value", value, 0);
  storeBytes(0x20011024, 4, 6);
  storeBytes(0x20011020, 4, 6);
  storeBytes(0x20000018, 4, 0x184021);
  storeBytes(0x20000058, 4, 1);
  storeBytes(0x200000c4, 4, 0x40);
  storeBytes(0x2000400c, 4, 0x1189);
  storeBytes(0x20004010, 4, 5);
  storeBytes(0x200111b4, 4, 1U << 9);
  expect("masked ASC1 poll", pollSti5518Devices() != 0, 0);
  expect("source nine mask defers ASC1 IRQ", interruptHandlerActive(), 0);
  readBytes(0x2001107c, 4, &value);
  expect("raw ASC1 source nine visible while masked", value, 1U << 9);
  storeBytes(0x200111b8, 4, 1U << 9);
  expect("ASC1 ready enters native IRQ", pollSti5518Devices() != 0, 0);
  expect("ASC1 interrupt level six", activeExternalInterruptLevel() == 6, 1);
  storeBytes(0x20004010, 4, 0);
  expect("ASC1 IRQ return", iret_(0) != 0, 0);
  storeBytes(0x20004004, 4, 0x41);
  storeBytes(0x2000500c, 4, 0x1189);
  storeBytes(0x20005004, 4, 0x42);
  auto asc1Bytes = takeSti5518Asc1Transmit();
  auto asc2Bytes = takeSti5518Asc2Transmit();
  expect("ASC1 transmit captures independently", asc1Bytes.size(), 1);
  expect("ASC2 transmit captures independently", asc2Bytes.size(), 1);
  if (asc1Bytes.size() == 1) expect("ASC1 captured byte", asc1Bytes[0], 0x41);
  if (asc2Bytes.size() == 1) expect("ASC2 captured byte", asc2Bytes[0], 0x42);
  expect("ASC1 capture drains", takeSti5518Asc1Transmit().empty(), 1);
  pollSti5518Devices();
  pollSti5518Devices();
  storeBytes(0x20004010, 4, 4);
  storeBytes(0x20005010, 4, 4);
  expect("both ready ASC ports poll", pollSti5518Devices() != 0, 0);
  readBytes(0x2001107c, 4, &value);
  expect("both raw ASC sources combine", value, (1U << 9) | (1U << 8));
  expect("both ASC ports share interrupt level", activeExternalInterruptLevel() == 6, 1);
  storeBytes(0x20004010, 4, 0);
  expect("shared ASC IRQ return", iret_(0) != 0, 0);
  expect("remaining ASC2 ready source polls", pollSti5518Devices() != 0, 0);
  expect("ASC1 disable does not suppress ASC2 IRQ", activeExternalInterruptLevel() == 6, 1);
  storeBytes(0x20005010, 4, 0);
  expect("remaining ASC2 IRQ return", iret_(0) != 0, 0);
  storeBytes(0x20004004, 4, 0x43);
  expect("ASC1 transmit delay begins", readBytes(0x20004014, 4, &value) != 0, 0);
  expect("ASC1 initially busy", value & 6U, 0);
  readBytes(0x20005014, 4, &value);
  expect("ASC1 write leaves ASC2 ready", value & 6U, 6);
  pollSti5518Devices();
  storeBytes(0x20005004, 4, 0x44);
  pollSti5518Devices();
  readBytes(0x20004014, 4, &value);
  expect("ASC1 delay completes independently", value & 6U, 6);
  readBytes(0x20005014, 4, &value);
  expect("ASC2 still has its own delay", value & 6U, 0);
  pollSti5518Devices();
  readBytes(0x20005014, 4, &value);
  expect("ASC2 delay completes independently", value & 6U, 6);
  expect("ASC1 RX rejects writes", storeBytes(0x20004008, 4, 1) == DEVICE_ACCESS_UNSUPPORTED, 1);
  expect("ASC1 status rejects writes", storeBytes(0x20004014, 4, 1) == DEVICE_ACCESS_UNSUPPORTED, 1);
  expect("ASC1 rejects byte accesses", readBytes(0x20004014, 1, &value) == INVALID_DEVICE_ACCESS, 1);
  shutdownSti5518Devices();
  schedulerFixture(0x20000);
}

static void testInterruptReturns() {
  testSti5512Devices();
  testSti5518Devices();
  for (unsigned char priority : {static_cast<unsigned char>(HIGH_PRIORITY), static_cast<unsigned char>(LOW_PRIORITY)}) {
    schedulerFixture(0x180000, priority);
    setPortableInstructionAbi(false);
    cpuState.areg = 0x1111;
    cpuState.breg = 0x2222;
    cpuState.creg = 0x3333;
    storeBytes(0x184018, 4, 0x80000000);
    storeBytes(0x18401c, 4, 0x9400);
    bool accepted = true;
    omrState.Enables &= ~static_cast<uint32_t>(LP_EXTERNALEVENT_ENB | HP_EXTERNALEVENT_ENB);
    expect("native IRQ masked", serviceNativeInterrupt(6, 0x184020, &accepted) != 0, 0);
    expect("native IRQ masking defers entry", accepted, 0);
    omrState.Enables |= LP_EXTERNALEVENT_ENB | HP_EXTERNALEVENT_ENB;
    expect("native IRQ enters without portable ABI", serviceNativeInterrupt(6, 0x184020, &accepted) != 0, 0);
    expect("native IRQ accepted", accepted, 1);
    expect("native IRQ save workspace", cpuState.wptr, 0x184000);
    expect("native IRQ handler Iptr", cpuState.iptr, 0x9400);
    expect("native IRQ returns", iret_(0) != 0, 0);
    expect("native IRQ restores priority", cpuState.priority, priority);
    expect("native IRQ restores workspace", cpuState.wptr, 0x180000);
    expect("native IRQ restores A", cpuState.areg, 0x1111);
    expect("native IRQ restores B", cpuState.breg, 0x2222);
    expect("native IRQ restores C", cpuState.creg, 0x3333);
  }
  static int acknowledged = 0;
  static uint32_t acknowledgedWorkspace = 0;
  acknowledged = 0;
  setInterruptReturnObserver(+[] {
    ++acknowledged;
    acknowledgedWorkspace = cpuState.wptr;
  });
  const uint32_t saved[] = {STATUS_VALID | STATUS_TIMESLICE_ENABLE, 0x181000,
                           0x9300, 0x80000000, 0x12345678, 0xffffffff};
  for (uint32_t base : {0x180000U, 0xfffffff0U}) {
    schedulerFixture(base, HIGH_PRIORITY);
    cpuState.move2dBlockLength = 3;
    cpuState.move2dDestStride = 4;
    cpuState.move2dSourceStride = 5;
    for (uint32_t index = 0; index < 6; ++index) storeBytes(base + index * 4U, 4, saved[index]);
    int previous = acknowledged;
    expect("iret restores saved high process", iret_(0) != 0, 0);
    expect("iret high Status", cpuState.status, saved[0]);
    expect("iret high workspace", cpuState.wptr, saved[1]);
    expect("iret high Iptr", cpuState.iptr, saved[2]);
    expect("iret high A", cpuState.areg, saved[3]);
    expect("iret high B", cpuState.breg, saved[4]);
    expect("iret high C", cpuState.creg, saved[5]);
    expect("iret high priority retained", cpuState.priority, HIGH_PRIORITY);
    expect("iret high process running", cpuState.running, 1);
    expect("iret preserves block parameters", cpuState.move2dBlockLength, 3);
    expect("iret acknowledges exactly once", static_cast<uint32_t>(acknowledged - previous), 1);
    expect("iret acknowledgement sees restored context", acknowledgedWorkspace, saved[1]);
  }
  schedulerFixture(0x182000);
  cpuState.areg = 0x1111;
  cpuState.breg = 0x2222;
  cpuState.creg = 0x3333;
  cpuState.status = 0x40;
  cpuState.move2dBlockLength = 7;
  cpuState.move2dDestStride = 8;
  cpuState.move2dSourceStride = 9;
  uint32_t previousMask = 0;
  changeTrapMask(LOW_PRIORITY, INTEGER_ERR_TRAPENB, true, &previousMask);
  changeTrapMask(HIGH_PRIORITY, BREAKPOINT_TRAPENB, true, &previousMask);
  storeBytes(0x183000 - 4, 4, 0x9400);
  enqueueProcess(0x183000, HIGH_PRIORITY);
  pollTimers();
  storeBytes(0x183000, 4, 0);
  expect("iret null Status restores low shadow", iret_(0) != 0, 0);
  expect("iret low workspace", cpuState.wptr, 0x182000);
  expect("iret low priority", cpuState.priority, LOW_PRIORITY);
  expect("iret low Iptr", cpuState.iptr, 0x3000);
  expect("iret low A", cpuState.areg, 0x1111);
  expect("iret low B", cpuState.breg, 0x2222);
  expect("iret low C", cpuState.creg, 0x3333);
  expect("iret low Status", cpuState.status, 0x40);
  expect("iret low block count", cpuState.move2dBlockLength, 7);
  expect("iret low destination stride", cpuState.move2dDestStride, 8);
  expect("iret low source stride", cpuState.move2dSourceStride, 9);
  expect("iret selects low trap mask", trapEnableMask(LOW_PRIORITY), INTEGER_ERR_TRAPENB);
  cpuState.running = false;
  storeBytes(0x184000 - 4, 4, 0x9500);
  enqueueProcess(0x184000, HIGH_PRIORITY);
  pollTimers();
  storeBytes(0x184000, 4, 0);
  expect("iret with consumed shadow returns idle", iret_(0) != 0, 0);
  expect("iret idle is not running", cpuState.running, 0);
  expect("iret idle workspace sentinel", cpuState.wptr, NO_PROCESS);
  omrState.Enables &= ~static_cast<uint32_t>(QUEUE_EMPTY_TRAPENB);
  expect("iret idle does not fetch stale handler code", decodeNextInstr() == CPU_IDLE, 1);

  schedulerFixture(0x185000);
  cpuState.areg = 0x4567;
  storeBytes(0x186000 - 4, 4, 0x9600);
  enqueueProcess(0x186000, HIGH_PRIORITY);
  pollTimers();
  storeBytes(0x186000, 4, 0);
  char directory[] = "/tmp/st20-iret-XXXXXX";
  expect("iret snapshot directory", mkdtemp(directory) != nullptr, 1);
  expect("save pending low-shadow return", saveCPUState(directory) != 0, 0);
  resetScheduler();
  expect("restore pending low-shadow return", loadCPUState(directory) != 0, 0);
  expect("iret works after snapshot", iret_(0) != 0, 0);
  expect("snapshot iret restores low workspace", cpuState.wptr, 0x185000);
  expect("snapshot iret restores low A", cpuState.areg, 0x4567);
  std::string filename = std::string(directory) + "/cpu.bin";
  unlink(filename.c_str());
  rmdir(directory);

  int previous = acknowledged;
  schedulerFixture(0x6d000000, HIGH_PRIORITY);
  cpuState.areg = 11;
  expect("iret missing Status fails", iret_(0) == READ_UNUSED_MEM, 1);
  storeBytes(0x6d000000, 4, STATUS_VALID);
  expect("iret missing frame tail fails", iret_(0) == READ_UNUSED_MEM, 1);
  expect("failed iret preserves workspace", cpuState.wptr, 0x6d000000);
  expect("failed iret preserves A", cpuState.areg, 11);
  expect("failed iret preserves Iptr", cpuState.iptr, 0x3000);
  expect("failed iret does not acknowledge", static_cast<uint32_t>(acknowledged), static_cast<uint32_t>(previous));
  schedulerFixture(0x187000, HIGH_PRIORITY);
  for (uint32_t index = 0; index < 6; ++index) storeBytes(0x187000 + index * 4U, 4, saved[index]);
  storeBytes(0x187004, 4, 0x181001);
  expect("iret rejects unaligned restored workspace", iret_(0) == INVALID_INTERRUPT_RETURN, 1);
  expect("bad iret workspace preserves active frame", cpuState.wptr, 0x187000);
  storeBytes(0x187000, 4, STATUS_VALID | (1U << 26));
  expect("iret rejects unsupported microcode restart", iret_(0) == INTERRUPTED_OPERATION_UNSUPPORTED, 1);
  expect("unsupported iret does not acknowledge", static_cast<uint32_t>(acknowledged), static_cast<uint32_t>(previous));
  cpuState.wptr = 0x187001;
  expect("iret rejects unaligned frame", iret_(0) == INVALID_INTERRUPT_RETURN, 1);
  schedulerFixture(0x187000);
  expect("iret rejects low-priority caller", iret_(0) == INVALID_INTERRUPT_RETURN, 1);
  schedulerFixture(0x187000, HIGH_PRIORITY);
  installTrap(HIGH_PRIORITY, 0, 0x188000, 0x9700);
  omrState.Enables |= BREAKPOINT_TRAPENB;
  requestTrap(BREAKPOINT_TRAPENB, HIGH_PRIORITY);
  expect("iret cannot consume a trap frame", iret_(0) == INVALID_INTERRUPT_RETURN, 1);
  expect("iret trap rejection leaves handler active", cpuState.wptr, 0x188000);
  expect("trap remains returnable with tret", tret_(0) != 0, 0);
  expect("rejected returns never acknowledge", static_cast<uint32_t>(acknowledged), static_cast<uint32_t>(previous));

  schedulerFixture(0x18a000);
  cpuState.status = 1U << 26;
  storeBytes(0x18b000 - 4, 4, 0x9900);
  enqueueProcess(0x18b000, HIGH_PRIORITY);
  pollTimers();
  storeBytes(0x18b000, 4, 0);
  expect("iret rejects interrupted low-shadow microcode", iret_(0) == INTERRUPTED_OPERATION_UNSUPPORTED, 1);
  expect("unsupported shadow return preserves handler workspace", cpuState.wptr, 0x18b000);
  expect("unsupported shadow return preserves handler priority", cpuState.priority, HIGH_PRIORITY);
  expect("unsupported shadow return does not acknowledge", static_cast<uint32_t>(acknowledged), static_cast<uint32_t>(previous));

  schedulerFixture(0x189000, HIGH_PRIORITY);
  for (uint32_t index = 0; index < 6; ++index) storeBytes(0x189000 + index * 4U, 4, saved[index]);
  cpuState.iptr = 0x9800;
  storeBytes(0x9800, 2, 0xff61);
  expect("encoded iret decode", decodeNextInstr() != 0, 0);
  int watchFlag = 0;
  expect("encoded iret executes", execInstr(&watchFlag) != 0, 0);
  expect("encoded iret installs saved Iptr", cpuState.iptr, saved[2]);
  expect("encoded iret installs saved A", cpuState.areg, saved[3]);
  setInterruptReturnObserver(nullptr);
  schedulerFixture(0x189000, HIGH_PRIORITY);
  int beforeDisabled = acknowledged;
  expect("iret works without a controller observer", iret_(0) != 0, 0);
  expect("disabled iret observer is not called", static_cast<uint32_t>(acknowledged), static_cast<uint32_t>(beforeDisabled));
  schedulerFixture(0x20000);
}

static void testResumableMoves() {
  setPortableInstructionAbi(true);
  schedulerFixture(0x1c1000);
  for (uint32_t index = 0; index < 16; ++index) storeBytes(0x1c2000 + index, 1, index + 1);
  storeBytes(0x1c4000 - 4, 4, 0xa500);
  enqueueProcess(0x1c4000, HIGH_PRIORITY);
  cpuState.areg = 16;
  cpuState.breg = 0x1c3000;
  cpuState.creg = 0x1c2000;
  expect("portable move preempts at checkpoint", move_(0) != 0, 0);
  expect("portable move high process runs", cpuState.wptr, 0x1c4000);
  stopp_(0);
  expect("portable move resumes low shadow", cpuState.wptr, 0x1c1000);
  expect("portable move retains remaining count", cpuState.areg, 12);
  char directory[] = "/tmp/st20-move-XXXXXX";
  expect("resumable snapshot directory", mkdtemp(directory) != nullptr, 1);
  expect("save resumable move", saveCPUState(directory) != 0, 0);
  resetScheduler();
  expect("load resumable move", loadCPUState(directory) != 0, 0);
  expect("decode pending move without fetching", decodeNextInstr() != 0, 0);
  int watchFlag = 0;
  expect("execute pending move", execInstr(&watchFlag) != 0, 0);
  for (uint32_t index = 0; index < 16; ++index) {
    uint32_t value = 0;
    readBytes(0x1c3000 + index, 1, &value);
    expect("resumable move exact data", value, index + 1);
  }
  std::string filename = std::string(directory) + "/cpu.bin";
  unlink(filename.c_str());
  rmdir(directory);
  struct Device { int reads{0}; int writes{0}; bool fail{true}; } device;
  MemoryDevice region{0x40040000, 0x4004000f, &device,
      +[](void *context, uint32_t, int, uint32_t *value) {
        ++static_cast<Device *>(context)->reads;
        *value = 0x12345678;
        return 0;
      },
      +[](void *context, uint32_t, int, uint32_t) {
        auto &state = *static_cast<Device *>(context);
        ++state.writes;
        return state.fail ? WRITE_FAILURE : 0;
      }};
  registerMemoryDevice(region);
  schedulerFixture(0x1c1000);
  cpuState.areg = 4;
  cpuState.breg = region.start + 8;
  cpuState.creg = region.start;
  expect("portable devmove caches successful read", devmove_(0) == WRITE_FAILURE, 1);
  expect("portable devmove read cache present", cpuState.status & STATUS_BUFFERED_WORD, STATUS_BUFFERED_WORD);
  char cacheDirectory[] = "/tmp/st20-cache-XXXXXX";
  expect("cached-write snapshot directory", mkdtemp(cacheDirectory) != nullptr, 1);
  expect("save cached successful read", saveCPUState(cacheDirectory) != 0, 0);
  resetScheduler();
  expect("restore cached successful read", loadCPUState(cacheDirectory) != 0, 0);
  device.fail = false;
  expect("portable devmove retries cached write", resumePendingOperation() != 0, 0);
  expect("portable devmove does not repeat MMIO read", static_cast<uint32_t>(device.reads), 1);
  expect("portable devmove attempts failed and successful writes", static_cast<uint32_t>(device.writes), 2);
  std::string cacheFile = std::string(cacheDirectory) + "/cpu.bin";
  unlink(cacheFile.c_str());
  rmdir(cacheDirectory);
  unregisterMemoryDevice(region.start);
  setPortableInstructionAbi(false);
  schedulerFixture(0x20000);
}

static void testPortableRestartAndShadow() {
  setPortableInstructionAbi(true);
  schedulerFixture(0x1d0000);
  cpuState.moveRowWidth = 16;
  cpuState.move2dBlockLength = 2;
  cpuState.move2dDestStride = 20;
  cpuState.move2dSourceStride = 24;
  cpuState.moveBufferedWord = 0x12345678;
  intdis_(0);
  cpuState.areg = 15;
  cpuState.breg = LOW_PRIORITY;
  cpuState.creg = 0x1d1000;
  expect("portable stshadow stores complete state", stshadow_(0) != 0, 0);
  uint32_t value = 0;
  readBytes(0x1d101c, 4, &value);
  expect("portable shadow row-width word", value, 16);
  readBytes(0x1d102c, 4, &value);
  expect("portable shadow cached word", value, 0x12345678);
  readBytes(0x1d1004, 4, &value);
  expect("portable shadow format tag", value & STATUS_PORTABLE_ABI, STATUS_PORTABLE_ABI);
  expect("portable shadow interruption flag", value & STATUS_PORTABLE_INTDIS, STATUS_PORTABLE_INTDIS);
  storeBytes(0x1d1004, 4, STATUS_VALID | STATUS_PORTABLE_ABI | STATUS_TIMESLICE_ENABLE);
  storeBytes(0x1d1008, 4, 0x1d2000);
  storeBytes(0x1d100c, 4, 0xa600);
  storeBytes(0x1d1010, 4, 0xaaaa);
  storeBytes(0x1d1014, 4, 0xbbbb);
  storeBytes(0x1d1018, 4, 0xcccc);
  cpuState.areg = 0x1d1000;
  expect("portable restart executes", restart_(0) != 0, 0);
  expect("portable restart workspace", cpuState.wptr, 0x1d2000);
  expect("portable restart entry", cpuState.iptr, 0xa600);
  expect("portable restart A", cpuState.areg, 0xaaaa);
  expect("portable restart row-width", cpuState.moveRowWidth, 16);
  expect("portable restart cached-word", cpuState.moveBufferedWord, 0x12345678);
  storeBytes(0x1d1004, 4, STATUS_VALID);
  cpuState.areg = 0x1d1000;
  expect("portable restart rejects untagged frame", restart_(0) == INVALID_PORTABLE_STATE, 1);
  expect("invalid restart preserves workspace", cpuState.wptr, 0x1d2000);
  storeBytes(0x1d1004, 4, STATUS_VALID | STATUS_PORTABLE_ABI | (1U << 26));
  storeBytes(0x1d1010, 4, 4);
  storeBytes(0x1d1014, 4, 0x1d4000);
  storeBytes(0x1d1018, 4, 0x1d3000);
  storeBytes(0x1d101c, 4, 4);
  storeBytes(0x1d1020, 4, 1);
  storeBytes(0x1d3000, 4, 0x89abcdef);
  cpuState.areg = 0x1d1000;
  expect("portable restart imports pending move", restart_(0) != 0, 0);
  expect("portable restarted move resumes", resumePendingOperation() != 0, 0);
  readBytes(0x1d4000, 4, &value);
  expect("portable restarted move data", value, 0x89abcdef);
  setPortableInstructionAbi(false);
  schedulerFixture(0x20000);
}

static void testPortableChannelCopy() {
  setPortableInstructionAbi(true);
  schedulerFixture(0x1e0000);
  storeBytes(0x1e1000, 4, NO_PROCESS);
  cpuState.areg = 16;
  cpuState.breg = 0x1e1000;
  cpuState.creg = 0x1e2000;
  expect("portable input waits for sender", in_(0) != 0, 0);
  cpuState.running = true;
  cpuState.wptr = 0x1e3000;
  cpuState.iptr = 0xa700;
  for (uint32_t index = 0; index < 16; ++index) storeBytes(0x1e4000 + index, 1, index + 1);
  storeBytes(0x1e5000 - 4, 4, 0xa800);
  enqueueProcess(0x1e5000, HIGH_PRIORITY);
  cpuState.areg = 16;
  cpuState.breg = 0x1e1000;
  cpuState.creg = 0x1e4000;
  expect("portable channel copy can preempt", out_(0) != 0, 0);
  expect("portable channel high process runs", cpuState.wptr, 0x1e5000);
  expect("peer remains blocked until copy finishes", omrState.ProcQueueFPtr[LOW_PRIORITY], NO_PROCESS);
  stopp_(0);
  expect("portable channel resumes sender", cpuState.wptr, 0x1e3000);
  expect("portable channel continuation completes", resumePendingOperation() != 0, 0);
  expect("portable channel releases receiver once", omrState.ProcQueueFPtr[LOW_PRIORITY], 0x1e0000);
  for (uint32_t index = 0; index < 16; ++index) {
    uint32_t value = 0;
    readBytes(0x1e2000 + index, 1, &value);
    expect("portable channel exact data", value, index + 1);
  }
  setPortableInstructionAbi(false);
  schedulerFixture(0x20000);
}

static void testPortableInterruptController() {
  setPortableInstructionAbi(true);
  schedulerFixture(0x1f0000);
  cpuState.areg = 0x1234;
  expect("queue portable external interrupt", queueExternalInterrupt(2, 0x1f1000, 0xa900) != 0, 0);
  expect("deliver portable external interrupt", pollTimers() != 0, 0);
  expect("portable interrupt handler workspace", cpuState.wptr, 0x1f1000);
  expect("portable interrupt active level", static_cast<uint32_t>(activeExternalInterruptLevel()), 2);
  expect("portable interrupt cannot deschedule", stopp_(0) == TRAP_HANDLER_BLOCKED, 1);
  queueExternalInterrupt(1, 0x1f2000, 0xaa00);
  pollTimers();
  expect("lower interrupt waits", cpuState.wptr, 0x1f1000);
  queueExternalInterrupt(5, 0x1f3000, 0xab00);
  pollTimers();
  expect("higher interrupt nests", cpuState.wptr, 0x1f3000);
  expect("nested iret restores outer", iret_(0) != 0, 0);
  expect("nested outer workspace restored", cpuState.wptr, 0x1f1000);
  expect("outer iret restores low", iret_(0) != 0, 0);
  expect("portable low interrupted A restored", cpuState.areg, 0x1234);
  pollTimers();
  expect("deferred lower interrupt delivered", cpuState.wptr, 0x1f2000);
  char directory[] = "/tmp/st20-irq-XXXXXX";
  expect("portable IRQ snapshot directory", mkdtemp(directory) != nullptr, 1);
  expect("save portable IRQ stack", saveCPUState(directory) != 0, 0);
  resetScheduler();
  expect("load portable IRQ stack", loadCPUState(directory) != 0, 0);
  expect("portable IRQ return after snapshot", iret_(0) != 0, 0);
  expect("snapshot IRQ restores low", cpuState.wptr, 0x1f0000);
  std::string filename = std::string(directory) + "/cpu.bin";
  unlink(filename.c_str());
  rmdir(directory);
  expect("portable IRQ invalid level", queueExternalInterrupt(8, 0x1f1000, 0) == INVALID_EXTERNAL_INTERRUPT, 1);
  setPortableInstructionAbi(false);
  schedulerFixture(0x20000);
}

static void testNativeExternalChannels() {
  setPortableInstructionAbi(false);
  constexpr uint32_t channel = 0x80000010;
  const uint8_t input[] = {0x12, 0x34};
  for (unsigned char priority : {static_cast<unsigned char>(HIGH_PRIORITY), static_cast<unsigned char>(LOW_PRIORITY)}) {
    schedulerFixture(0x250000);
    cpuState.priority = priority;
    expect("register native modeled channel", registerExternalChannel(channel, 8) != 0, 0);
    expect("native channel registration keeps ABI disabled", portableInstructionAbiEnabled(), 0);
    expect("native modeled ALT starts", alt_(0) != 0, 0);
    cpuState.areg = 1;
    cpuState.breg = channel;
    expect("native modeled ALT guard enables", enbc_(0) != 0, 0);
    expect("native modeled ALT feed", feedExternalChannel(channel, input) != 0, 0);
    expect("native modeled ALT wait is ready", altwt_(0) != 0, 0);
    cpuState.areg = 12;
    cpuState.breg = 1;
    cpuState.creg = channel;
    expect("native modeled ALT guard disables", disc_(0) != 0, 0);
    expect("native modeled ALT selected", cpuState.areg, 1);
    uint32_t offset = 0;
    readBytes(cpuState.wptr, 4, &offset);
    expect("native modeled ALT branch offset", offset, 12);
    cpuState.areg = channel;
    expect("native modeled channel reset", resetch_(0) != 0, 0);
    expect("native modeled reset no waiter", cpuState.areg, NO_PROCESS);
    expect("native modeled ready input feed", feedExternalChannel(channel, input) != 0, 0);
    reset(2, channel, 0x251000);
    expect("native modeled input completes", in_(0) != 0, 0);
    uint32_t value = 0;
    readBytes(0x251000, 2, &value);
    expect("native modeled input bytes", value, 0x3412);
    reset(2, channel, 0x251000);
    expect("native modeled output completes", out_(0) != 0, 0);
    uint8_t output[8]{};
    std::size_t count = 0;
    expect("native modeled output drains", drainExternalChannel(channel, output, &count) != 0, 0);
    expect("native modeled output count", count, 2);
    expect("native modeled first output byte", output[0], 0x12);
    expect("native modeled second output byte", output[1], 0x34);
    reset(0x56, channel, 0);
    expect("native modeled outbyte", outbyte_(0) != 0, 0);
    reset(0x12345678, channel, 0);
    expect("native modeled outword", outword_(0) != 0, 0);
    drainExternalChannel(channel, output, &count);
    expect("native modeled scalar output count", count, 5);
    const uint8_t expected[] = {0x56, 0x78, 0x56, 0x34, 0x12};
    for (std::size_t index = 0; index < 5; ++index)
      expect("native modeled scalar byte order", output[index], expected[index]);
    reset(2, channel, 0x251004);
    expect("native modeled input blocks", in_(0) != 0, 0);
    expect("native modeled input suspended", cpuState.running, 0);
    char directory[] = "/tmp/st20-native-fifo-XXXXXX";
    expect("native modeled snapshot directory", mkdtemp(directory) != nullptr, 1);
    expect("native modeled blocked snapshot saves", saveCPUState(directory) != 0, 0);
    reset(channel, 0, 0);
    resetch_(0);
    unregisterExternalChannel(channel);
    expect("native modeled blocked snapshot restores", loadCPUState(directory) != 0, 0);
    expect("native modeled input wakes", feedExternalChannel(channel, input) != 0, 0);
    expect("native modeled input resumed", cpuState.running, 1);
    expect("native modeled input has no portable continuation", cpuState.status & (STATUS_PORTABLE_ABI | STATUS_INTERRUPTED_OPERATION_MASK), 0);
    readBytes(0x251004, 2, &value);
    expect("native modeled blocked input bytes", value, 0x3412);
    std::string filename = std::string(directory) + "/cpu.bin";
    unlink(filename.c_str());
    rmdir(directory);
    storeBytes(0x251008, 4, 0x01020304);
    storeBytes(0x25100c, 4, 0x05060708);
    reset(8, channel, 0x251008);
    expect("native modeled output fills FIFO", out_(0) != 0, 0);
    reset(0x5a, channel, 0);
    expect("native modeled full output blocks", outbyte_(0) != 0, 0);
    expect("native modeled writer suspended", cpuState.running, 0);
    expect("native modeled drain wakes writer", drainExternalChannel(channel, output, &count) != 0, 0);
    expect("native modeled writer resumed", cpuState.running, 1);
    expect("native modeled writer has no portable continuation", cpuState.status & (STATUS_PORTABLE_ABI | STATUS_INTERRUPTED_OPERATION_MASK), 0);
    drainExternalChannel(channel, output, &count);
    expect("native modeled blocked scalar count", count, 1);
    expect("native modeled blocked scalar byte", output[0], 0x5a);
    expect("native modeled failure test feeds input", feedExternalChannel(channel, input) != 0, 0);
    for (uint32_t buffer : {0x20004000U, 0x1fffffffU}) {
      reset(2, channel, buffer);
      expect("native modeled input rejects peripheral span", in_(0) == DEVICE_ACCESS_UNSUPPORTED, 1);
    }
    uint32_t deviceTouches = 0;
    MemoryDevice device{0x40030000, 0x40030003, &deviceTouches,
        +[](void *context, uint32_t, int, uint32_t *) {
          ++*static_cast<uint32_t *>(context);
          return READ_UNUSED_MEM;
        },
        +[](void *context, uint32_t, int, uint32_t) {
          ++*static_cast<uint32_t *>(context);
          return WRITE_FAILURE;
        }};
    expect("native modeled RAM policy registers test device", registerMemoryDevice(device) != 0, 0);
    for (uint32_t buffer : {device.start, device.start - 1U}) {
      reset(2, channel, buffer);
      expect("native modeled input rejects mapped buffer", in_(0) == DEVICE_ACCESS_UNSUPPORTED, 1);
    }
    expect("native modeled buffer rejection has no MMIO effect", deviceTouches, 0);
    expect("native modeled RAM policy removes test device", unregisterMemoryDevice(device.start) != 0, 0);
    reset(2, channel, 0xffffffff);
    expect("native modeled input rejects wrapped span", in_(0) == INVALID_CHANNEL, 1);
    reset(2, channel, 0x251010);
    expect("native modeled rejected input retained FIFO", in_(0) != 0, 0);
    readBytes(0x251010, 2, &value);
    expect("native modeled rejected input retains exact bytes", value, 0x3412);
    reset(2, channel, 0x6e123400);
    expect("native modeled output reports unreadable RAM", out_(0) == READ_UNUSED_MEM, 1);
    drainExternalChannel(channel, output, &count);
    expect("native modeled output failure leaves FIFO empty", count, 0);
    reset(0, channel, 0x20004000);
    expect("native modeled zero input ignores buffer", in_(0) != 0, 0);
    reset(0, channel, 0x20004000);
    expect("native modeled zero output ignores buffer", out_(0) != 0, 0);
    reset(9, channel, 0x251000);
    expect("native modeled oversized message rejected", out_(0) == INVALID_CHANNEL, 1);
    expect("native modeled blocked ALT starts", alt_(0) != 0, 0);
    reset(1, channel, 0);
    expect("native modeled blocked ALT enables", enbc_(0) != 0, 0);
    expect("native modeled blocked ALT waits", altwt_(0) != 0, 0);
    expect("native modeled ALT owner suspended", cpuState.running, 0);
    expect("native modeled ALT feed wakes owner", feedExternalChannel(channel, input) != 0, 0);
    expect("native modeled ALT owner resumed", cpuState.running, 1);
    reset(16, 1, channel);
    expect("native modeled awakened ALT disables", disc_(0) != 0, 0);
    expect("native modeled awakened ALT selects", cpuState.areg, 1);
    reset(2, channel, 0x251014);
    expect("native modeled selected ALT input completes", in_(0) != 0, 0);
    uint32_t previous = 0;
    expect("native modeled completion trap enables", changeTrapMask(priority, EXTERNALCH_TRAPENB, true, &previous) != 0, 0);
    uint32_t handlerRecord = trapRecordAddress(priority, TRAP_GROUP_SCHEDULER, false);
    storeBytes(handlerRecord, 4, 0xffff0000);
    storeBytes(handlerRecord + 4, 4, 0x80000000);
    storeBytes(handlerRecord + 8, 4, 3);
    storeBytes(handlerRecord + 12, 4, 0xa900);
    storeBytes(0x251018, 4, 0xdeadbeef);
    cpuState.areg = 2;
    cpuState.breg = channel;
    cpuState.creg = 0x251018;
    expect("native modeled trapped input blocks", in_(0) != 0, 0);
    expect("native modeled invalid completion handler rejected", feedExternalChannel(channel, input) == INVALID_TRAP_HANDLER, 1);
    readBytes(0x251018, 4, &value);
    expect("native modeled invalid handler preserves input buffer", value, 0xdeadbeef);
    uint32_t owner = cpuState.wptr;
    storeBytes(handlerRecord + 8, 4, 0x254000);
    expect("native modeled completion retry enters kernel", pollTimers() != 0, 0);
    expect("native modeled completion kernel active", trapHandlerActive(), 1);
    expect("native modeled completion kernel descriptor", cpuState.breg, owner | priority);
    readBytes(0x251018, 2, &value);
    expect("native modeled data committed before kernel", value, 0x3412);
    expect("native modeled kernel owns enqueue", omrState.ProcQueueFPtr[priority], NO_PROCESS);
    char trapDirectory[] = "/tmp/st20-native-channel-trap-XXXXXX";
    expect("native modeled trapped snapshot directory", mkdtemp(trapDirectory) != nullptr, 1);
    expect("native modeled completed transfer snapshot saves", saveCPUState(trapDirectory) != 0, 0);
    cpuState.areg = channel;
    resetch_(0);
    unregisterExternalChannel(channel);
    expect("native modeled completed transfer snapshot restores", loadCPUState(trapDirectory) != 0, 0);
    expect("native modeled kernel enqueues owner", enqueueProcess(owner, priority) != 0, 0);
    expect("native modeled completion kernel returns", tret_(0) != 0, 0);
    expect("native modeled post-trap scheduler dispatches", pollTimers() != 0, 0);
    expect("native modeled completed owner resumes", cpuState.wptr, owner);
    expect("native modeled completed owner running", cpuState.running, 1);
    expect("native modeled trap return has no portable state", cpuState.status & (STATUS_PORTABLE_ABI | STATUS_INTERRUPTED_OPERATION_MASK), 0);
    filename = std::string(trapDirectory) + "/cpu.bin";
    unlink(filename.c_str());
    rmdir(trapDirectory);
    reset(2, channel, 0x251010);
    expect("native modeled reset waiter blocks", in_(0) != 0, 0);
    reset(channel, 0, 0);
    expect("native modeled reset cancels waiter", resetch_(0) != 0, 0);
    expect("native modeled reset returns descriptor", cpuState.areg, cpuState.wptr | cpuState.priority);
    expect("native modeled reset does not wake waiter", cpuState.running, 0);
    expect("unregister native modeled channel", unregisterExternalChannel(channel) != 0, 0);
  }
  schedulerFixture(0x20000);
}

static void testPortableExternalChannels() {
  setPortableInstructionAbi(true);
  constexpr uint32_t channel = 0x80000010;
  expect("register portable external channel", registerExternalChannel(channel, 8) != 0, 0);
  schedulerFixture(0x200000);
  cpuState.areg = 4;
  cpuState.breg = channel;
  cpuState.creg = 0x201000;
  expect("external input blocks without data", in_(0) != 0, 0);
  expect("external input suspended", cpuState.running, 0);
  const uint8_t input[] = {1, 2, 3, 4};
  expect("external host feed wakes input", feedExternalChannel(channel, input) != 0, 0);
  expect("external input context running", cpuState.running, 1);
  expect("external input continuation", resumePendingOperation() != 0, 0);
  uint32_t value = 0;
  readBytes(0x201000, 4, &value);
  expect("external input exact bytes", value, 0x04030201);
  cpuState.areg = 4;
  cpuState.breg = channel;
  cpuState.creg = 0x201000;
  expect("external output completes", out_(0) != 0, 0);
  uint8_t output[8]{};
  std::size_t count = 0;
  expect("external host drains output", drainExternalChannel(channel, output, &count) != 0, 0);
  expect("external output byte count", count, 4);
  for (std::size_t index = 0; index < 4; ++index) expect("external output data", output[index], input[index]);
  for (int iteration = 0; iteration < 2; ++iteration) {
    cpuState.areg = 4;
    cpuState.breg = channel;
    cpuState.creg = 0x201000;
    out_(0);
  }
  cpuState.areg = 0x5a;
  cpuState.breg = channel;
  expect("bounded external output blocks", outbyte_(0) != 0, 0);
  expect("bounded external output suspended", cpuState.running, 0);
  expect("draining external output wakes writer", drainExternalChannel(channel, output, &count) != 0, 0);
  expect("external scalar output resumes", resumePendingOperation() != 0, 0);
  drainExternalChannel(channel, output, &count);
  expect("external scalar output byte", output[0], 0x5a);
  schedulerFixture(0x202000);
  alt_(0);
  cpuState.areg = 1;
  cpuState.breg = channel;
  cpuState.creg = 0;
  expect("external ALT guard enables", enbc_(0) != 0, 0);
  expect("external ALT feed marks ready", feedExternalChannel(channel, input) != 0, 0);
  expect("external ALT wait does not block", altwt_(0) != 0, 0);
  cpuState.areg = 4;
  cpuState.breg = 1;
  cpuState.creg = channel;
  expect("external ALT guard disables", disc_(0) != 0, 0);
  expect("external ALT guard selected", cpuState.areg, 1);
  cpuState.areg = channel;
  expect("external channel reset", resetch_(0) != 0, 0);
  expect("unregister portable external channel", unregisterExternalChannel(channel) != 0, 0);
  setPortableInstructionAbi(false);
  schedulerFixture(0x20000);
}

static void testPortableMove2d() {
  setPortableInstructionAbi(true);
  for (uint32_t operation : {3U, 4U, 5U}) {
    schedulerFixture(0x210000);
    cpuState.move2dBlockLength = 2;
    cpuState.move2dDestStride = 8;
    cpuState.move2dSourceStride = 8;
    for (uint32_t index = 0; index < 16; ++index) {
      storeBytes(0x211000 + index, 1, index % 3 == 0 ? 0 : index);
      storeBytes(0x212000 + index, 1, 0xa5);
    }
    storeBytes(0x213000 - 4, 4, 0xac00);
    enqueueProcess(0x213000, HIGH_PRIORITY);
    expect("portable 2D preemption", beginResumableMove(operation, 4, 0x212000, 0x211000) != 0, 0);
    expect("portable 2D high process", cpuState.wptr, 0x213000);
    stopp_(0);
    expect("portable 2D continuation", resumePendingOperation() != 0, 0);
    for (uint32_t index = 0; index < 16; ++index) {
      uint32_t byte = index % 3 == 0 ? 0 : index;
      bool inRow = index % 8 < 4;
      bool selected = operation == 3 || (operation == 4 ? byte == 0 : byte != 0);
      uint32_t value = 0;
      readBytes(0x212000 + index, 1, &value);
      expect("portable 2D filter/stride data", value, inRow && selected ? byte : 0xa5U);
    }
  }
  CPUSTATE invalid = cpuState;
  invalid.status = STATUS_PORTABLE_ABI | (1U << 26) | STATUS_BUFFERED_WORD | STATUS_BUFFER_WIDTH;
  invalid.running = true;
  invalid.areg = 1;
  invalid.moveRowWidth = 1;
  invalid.move2dBlockLength = 1;
  expect("portable pending cache bounds validated", validPendingOperation(invalid), 0);
  setPortableInstructionAbi(false);
  schedulerFixture(0x20000);
}

static void testPortableExternalSnapshots() {
  setPortableInstructionAbi(true);
  constexpr uint32_t channel = 0x80000018;
  registerExternalChannel(channel, 8);
  schedulerFixture(0x220000);
  storeBytes(0x221000, 4, 0x44332211);
  cpuState.areg = 4;
  cpuState.breg = channel;
  cpuState.creg = 0x221000;
  out_(0);
  cpuState.areg = 4;
  cpuState.breg = channel;
  cpuState.creg = 0x222000;
  in_(0);
  char directory[] = "/tmp/st20-fifo-XXXXXX";
  expect("FIFO snapshot directory", mkdtemp(directory) != nullptr, 1);
  expect("save FIFO and blocked owner", saveCPUState(directory) != 0, 0);
  cpuState.areg = channel;
  resetch_(0);
  unregisterExternalChannel(channel);
  expect("restore FIFO and blocked owner", loadCPUState(directory) != 0, 0);
  uint8_t output[4]{};
  std::size_t count = 0;
  expect("snapshot restores queued output", drainExternalChannel(channel, output, &count) != 0, 0);
  expect("snapshot queued output size", count, 4);
  expect("snapshot queued output data", output[0], 0x11);
  const uint8_t input[] = {5, 6, 7, 8};
  expect("snapshot restores input waiter", feedExternalChannel(channel, input) != 0, 0);
  expect("snapshot input continuation", resumePendingOperation() != 0, 0);
  uint32_t value = 0;
  readBytes(0x222000, 4, &value);
  expect("snapshot input data", value, 0x08070605);
  unregisterExternalChannel(channel);
  std::string filename = std::string(directory) + "/cpu.bin";
  unlink(filename.c_str());
  rmdir(directory);
  schedulerFixture(0x223000);
  for (uint32_t index = 0; index < 12; ++index) storeBytes(0x224000 + index, 1, index + 9);
  queueExternalInterrupt(3, 0x226000, 0xad00);
  expect("copy suspends for external IRQ", beginResumableMove(1, 12, 0x225000, 0x224000) != 0, 0);
  expect("copy IRQ handler active", cpuState.wptr, 0x226000);
  expect("iret restores pending copy", iret_(0) != 0, 0);
  expect("copy continues after iret", resumePendingOperation() != 0, 0);
  for (uint32_t index = 0; index < 12; ++index) {
    readBytes(0x225000 + index, 1, &value);
    expect("IRQ-resumed copy data", value, index + 9);
  }
  setPortableInstructionAbi(false);
  schedulerFixture(0x20000);
}

static void testPeriodicTimeslicing() {
  constexpr uint64_t period = CPU_CLOCK / 1000;
  auto fixture = [] {
    schedulerFixture(0x1b0000);
    cpuState.status = STATUS_TIMESLICE_ENABLE;
    storeBytes(0x1b1000 - 4, 4, 0xa200);
    enqueueProcess(0x1b1000, LOW_PRIORITY);
    cpuState.iptr = 0xa100;
    storeBytes(0xa100, 1, 0x01);
  };
  fixture();
  advanceTimers(period);
  expect("periodic first boundary retains process", cpuState.timeslicePeriods, 1);
  expect("periodic first j does not rotate", j_(1) != 0, 0);
  expect("periodic first quantum still active", cpuState.wptr, 0x1b0000);
  advanceTimers(period);
  cpuState.iptr = 0xa100;
  decodeNextInstr();
  int watchFlag = 0;
  expect("periodic second boundary j rotates", execInstr(&watchFlag) != 0, 0);
  expect("periodic next low process runs", cpuState.wptr, 0x1b1000);
  expect("periodic old process is queued", omrState.ProcQueueBPtr[LOW_PRIORITY], 0x1b0000);
  uint32_t value = 0;
  readBytes(0x1b0000 - 4, 4, &value);
  expect("periodic j finishes before yielding", value, 0xa102);
  expect("new quantum starts fresh", cpuState.timeslicePeriods, 0);
  fixture();
  advanceTimers(period * 2);
  cpuState.areg = 0;
  settimeslice_(0);
  j_(1);
  expect("disabled Status prevents periodic yield", cpuState.wptr, 0x1b0000);
  cpuState.areg = 1;
  settimeslice_(0);
  intdis_(0);
  j_(1);
  expect("intdis prevents periodic yield", cpuState.wptr, 0x1b0000);
  intenb_(0);
  omrState.Enables &= ~static_cast<uint32_t>(LP_TIMESLICE_ENB);
  j_(1);
  expect("global mask prevents periodic yield", cpuState.wptr, 0x1b0000);
  omrState.Enables |= LP_TIMESLICE_ENB;
  j_(1);
  expect("reenabling releases expired quantum", cpuState.wptr, 0x1b1000);
  fixture();
  advanceTimers(period * 2);
  cpuState.iptr = 0xa100;
  storeBytes(0xa100, 1, 0x41);
  decodeNextInstr();
  expect("non-timeslicing instruction executes", execInstr(&watchFlag) != 0, 0);
  expect("non-timeslicing point does not rotate", cpuState.wptr, 0x1b0000);
  storeBytes(0x1b2000 - 4, 4, 0xa300);
  enqueueProcess(0x1b2000, HIGH_PRIORITY);
  pollTimers();
  advanceTimers(period);
  expect("high process does not age low counter", cpuState.timeslicePeriods, 0);
  stopp_(0);
  expect("expired low quantum survives preemption", cpuState.timeslicePeriods, 2);
  j_(1);
  expect("restored low quantum rotates at j", cpuState.wptr, 0x1b1000);
  fixture();
  advanceTimers(period * 2);
  expect("decode low instruction before priority race", decodeNextInstr() != 0, 0);
  storeBytes(0x1b2000 - 4, 4, 0xa300);
  enqueueProcess(0x1b2000, HIGH_PRIORITY);
  expect("high preemption wins over periodic slice", execInstr(&watchFlag) != 0, 0);
  expect("simultaneous high process runs first", cpuState.wptr, 0x1b2000);
  stopp_(0);
  expect("periodic race preserves interrupted low context", cpuState.wptr, 0x1b0000);
  expect("periodic race preserves expired quantum", cpuState.timeslicePeriods, 2);
  j_(1);
  expect("periodic race yields at next low point", cpuState.wptr, 0x1b1000);
  fixture();
  advanceTimers(period + period / 2);
  char directory[] = "/tmp/st20-periodic-XXXXXX";
  expect("periodic snapshot directory", mkdtemp(directory) != nullptr, 1);
  expect("save periodic quantum", saveCPUState(directory) != 0, 0);
  resetScheduler();
  expect("restore periodic quantum", loadCPUState(directory) != 0, 0);
  expect("periodic snapshot retains elapsed periods", cpuState.timeslicePeriods, 1);
  advanceTimers(period - period / 2);
  j_(1);
  expect("periodic snapshot retains clock phase", cpuState.wptr, 0x1b1000);
  std::string filename = std::string(directory) + "/cpu.bin";
  FILE *file = std::fopen(filename.c_str(), "rb+");
  expect("open periodic compatibility fixture", file != nullptr, 1);
  if (file) {
    std::fseek(file, 0, SEEK_END);
    int64_t runtimeStart = std::ftell(file) - static_cast<int64_t>(cpuRuntimeSnapshotSize());
    uint32_t version = 5;
    std::fseek(file, runtimeStart + static_cast<uint32_t>(sizeof(uint32_t)), SEEK_SET);
    std::fwrite(&version, sizeof(version), 1, file);
    std::fflush(file);
    expect("truncate to version5 runtime", ftruncate(fileno(file), runtimeStart + static_cast<uint32_t>(180 * sizeof(uint32_t))) != 0, 0);
    std::fclose(file);
    cpuState.timeslicePeriods = 2;
    omrState.TimeslicePhase = 123;
    expect("reject version5 periodic fixture", loadCPUState(directory) == INVALID_CPU_FILE, 1);
    expect("version5 rejection preserves quantum", cpuState.timeslicePeriods, 2);
    expect("version5 rejection preserves period phase", omrState.TimeslicePhase, 123);
  }
  unlink(filename.c_str());
  rmdir(directory);
  fixture();
  installTrap(LOW_PRIORITY, 3, 0x1b3000, 0xa400);
  omrState.Enables |= TIMESLICE_TRAPENB;
  advanceTimers(period * 2);
  expect("periodic j delivers scheduler trap", j_(1) != 0, 0);
  expect("periodic scheduler handler", cpuState.wptr, 0x1b3000);
  expect("periodic scheduler caller descriptor", cpuState.breg, 0x1b0001);
  cpuState.areg = cpuState.breg;
  runp_(0);
  tret_(0);
  pollTimers();
  expect("periodic kernel enqueues before dispatch", cpuState.wptr, 0x1b1000);
  schedulerFixture(0x20000);
}

static void testTimesliceTrapsAndSnapshots() {
  for (unsigned char priority : {static_cast<unsigned char>(HIGH_PRIORITY), static_cast<unsigned char>(LOW_PRIORITY)}) {
    schedulerFixture(0x176000, priority);
    installTrap(priority, 3, 0x177000, 0x8d00);
    omrState.Enables |= TIMESLICE_TRAPENB;
    storeBytes(0x178000 - 4, 4, 0x8e00);
    enqueueProcess(0x178000, priority);
    expect("timeslice scheduler trap enters", timeslice_(0) != 0, 0);
    expect("timeslice handler selected", cpuState.wptr, 0x177000);
    expect("timeslice kernel caller descriptor", cpuState.breg, 0x176000U | priority);
    expect("timeslice kernel owns enqueue", omrState.ProcQueueBPtr[priority], 0x178000);
    cpuState.areg = cpuState.breg;
    expect("timeslice kernel can enqueue caller", runp_(0) != 0, 0);
    expect("timeslice kernel cannot timeslice", timeslice_(0) == TRAP_HANDLER_BLOCKED, 1);
    expect("timeslice kernel return", tret_(0) != 0, 0);
    expect("timeslice return dispatches queue", pollTimers() != 0, 0);
    expect("timeslice does not resume caller prematurely", cpuState.wptr, 0x178000);
    stopp_(0);
    expect("timeslice caller resumes after waiter", cpuState.wptr, 0x176000);
  }
  schedulerFixture(0x179000);
  char directory[] = "/tmp/st20-interrupt-XXXXXX";
  expect("interrupt snapshot directory", mkdtemp(directory) != nullptr, 1);
  intdis_(0);
  cpuState.status &= ~STATUS_TIMESLICE_ENABLE;
  expect("save disabled interruption snapshot", saveCPUState(directory) != 0, 0);
  intenb_(0);
  expect("restore disabled interruption snapshot", loadCPUState(directory) != 0, 0);
  expect("snapshot restores intdis state", cpuState.interruptsDisabled, 1);
  expect("snapshot restores timeslice Status", cpuState.status & STATUS_TIMESLICE_ENABLE, 0);
  installTrap(LOW_PRIORITY, 0, 0x17a000, 0x8f00);
  omrState.Enables |= BREAKPOINT_TRAPENB;
  requestTrap(BREAKPOINT_TRAPENB, LOW_PRIORITY);
  expect("save trap caller interruption snapshot", saveCPUState(directory) != 0, 0);
  cpuState.interruptsDisabled = false;
  expect("restore trap caller interruption snapshot", loadCPUState(directory) != 0, 0);
  expect("restore trap caller return", tret_(0) != 0, 0);
  expect("trap return restores caller intdis", cpuState.interruptsDisabled, 1);
  std::string filename = std::string(directory) + "/cpu.bin";
  FILE *file = std::fopen(filename.c_str(), "rb+");
  expect("open interrupt compatibility fixture", file != nullptr, 1);
  if (file) {
    std::fseek(file, 0, SEEK_END);
    int64_t length = std::ftell(file);
    int64_t runtimeStart = length - static_cast<int64_t>(cpuRuntimeSnapshotSize());
    uint32_t invalidFlag = 2;
    std::fseek(file, runtimeStart + static_cast<uint32_t>(170 * sizeof(uint32_t)), SEEK_SET);
    std::fwrite(&invalidFlag, sizeof(invalidFlag), 1, file);
    std::fflush(file);
    expect("reject nonboolean interrupt state snapshot", loadCPUState(directory) == INVALID_CPU_FILE, 1);
    expect("failed interrupt snapshot preserves disable", cpuState.interruptsDisabled, 1);
    uint32_t version = 4;
    std::fseek(file, runtimeStart + static_cast<uint32_t>(sizeof(uint32_t)), SEEK_SET);
    std::fwrite(&version, sizeof(version), 1, file);
    std::fflush(file);
    expect("truncate to version4 runtime", ftruncate(fileno(file), runtimeStart + static_cast<uint32_t>(170 * sizeof(uint32_t))) != 0, 0);
    std::fclose(file);
    expect("reject version4 interrupt fixture", loadCPUState(directory) == INVALID_CPU_FILE, 1);
    expect("version4 rejection preserves interruption disable", cpuState.interruptsDisabled, 1);
    expect("version4 rejection does not restore old trap", trapHandlerActive(), 0);
  }
  unlink(filename.c_str());
  rmdir(directory);
  schedulerFixture(0x17b000);
  storeBytes(0x17c000 - 4, 4, 0x9000);
  enqueueProcess(0x17c000, LOW_PRIORITY);
  omrState.Enables |= TIMESLICE_TRAPENB;
  storeBytes(trapRecordAddress(LOW_PRIORITY, 3, false) + 8, 4, 0);
  cpuState.areg = 1;
  cpuState.breg = 2;
  cpuState.creg = 3;
  intdis_(0);
  expect("timeslice invalid handler rejected", timeslice_(0) == INVALID_TRAP_HANDLER, 1);
  expect("failed timeslice keeps original front", omrState.ProcQueueFPtr[LOW_PRIORITY], 0x17c000);
  expect("failed timeslice keeps original back", omrState.ProcQueueBPtr[LOW_PRIORITY], 0x17c000);
  expect("failed timeslice preserves caller workspace", cpuState.wptr, 0x17b000);
  expect("failed timeslice preserves caller A", cpuState.areg, 1);
  expect("failed timeslice preserves caller disable", cpuState.interruptsDisabled, 1);
  expect("failed timeslice caller remains running", cpuState.running, 1);
  schedulerFixture(0x17d000);
  installTrap(HIGH_PRIORITY, 3, 0x17e000, 0x9100);
  uint32_t previous = 0;
  changeTrapMask(HIGH_PRIORITY, TIMER_TRAPENB, true, &previous);
  storeBytes(0x17f000 - 4, 4, 0x9200);
  storeBytes(0x17f000 - 20, 4, 1);
  storeBytes(0x17f000 - 16, 4, NO_PROCESS);
  swapTimerList(HIGH_PRIORITY, 0x17f000, &previous);
  intdis_(0);
  expect("disabled interruption defers high timer kernel", advanceTimers(61) != 0, 0);
  expect("high timer kernel leaves disabled caller active", cpuState.wptr, 0x17d000);
  expect("disabled timer delivery keeps caller priority", cpuState.priority, LOW_PRIORITY);
  intenb_(0);
  expect("intenb releases pending high timer kernel", pollTimers() != 0, 0);
  expect("pending timer kernel selected after intenb", cpuState.wptr, 0x17e000);
  expect("pending timer descriptor retained", cpuState.breg, 0x17f000);
  expect("pending timer kernel returns", tret_(0) != 0, 0);
  schedulerFixture(0x20000);
}

static void testFractionAndParallelTraps() {
  for (unsigned char priority : {static_cast<unsigned char>(HIGH_PRIORITY), static_cast<unsigned char>(LOW_PRIORITY)}) {
    schedulerFixture(0x161000, priority);
    installTrap(priority, 1, 0x162000, 0x8200);
    omrState.Enables |= INTEGER_OVF_TRAPENB;
    cpuState.areg = 0x80000000;
    cpuState.breg = 0x80000000;
    cpuState.creg = 0x1234;
    expect("fmul overflow trap delivers", fmul_(0) != 0, 0);
    expect("fmul overflow handler selected", cpuState.wptr, 0x162000);
    expect("fmul overflow A undefined", cpuState.areg, undefinedWord);
    expect("fmul overflow B stack", cpuState.breg, 0x1234);
    uint32_t status = 0;
    readBytes(trapRecordAddress(priority, 1, true) + 4, 4, &status);
    expect("fmul overflow cause", status & 7U, INTEGER_OVF_TRAPENB);
    expect("fmul enabled trap no error flag", omrState.HP_ErrorFlag | omrState.LP_ErrorFlag, 0);
    expect("fmul overflow trap returns", tret_(0) != 0, 0);
    schedulerFixture(0x161000, priority);
    omrState.HaltOnErrorFlag = 1;
    omrState.Enables |= INTEGER_ERR_TRAPENB;
    cpuState.areg = 0x80000000;
    cpuState.breg = 0x80000000;
    expect("fmul masked overflow halts", fmul_(0) == CPU_HALTED, 1);
    expect("fmul halt latched", omrState.halted, 1);
    expect("fmul halt current-priority flag", priority == HIGH_PRIORITY ? omrState.HP_ErrorFlag : omrState.LP_ErrorFlag, 1);
    schedulerFixture(0x161000, priority);
    installTrap(priority, 0, 0x162000, 0x8200);
    omrState.Enables |= BREAKPOINT_TRAPENB;
    requestTrap(BREAKPOINT_TRAPENB, priority);
    storeBytes(0x163004, 4, 2);
    cpuState.areg = 0x163000;
    expect("endp trap handler retirement rejected", endp_(0) == TRAP_HANDLER_BLOCKED, 1);
    readBytes(0x163004, 4, &status);
    expect("rejected endp count unchanged", status, 2);
    expect("rejected endp handler still running", cpuState.running, 1);
    expect("endp handler can return", tret_(0) != 0, 0);
  }
  schedulerFixture(0x20000);
}

static void testReadyQueueNotifications() {
  for (unsigned char priority : {static_cast<unsigned char>(HIGH_PRIORITY), static_cast<unsigned char>(LOW_PRIORITY)}) {
    for (int mode = 0; mode < 3; ++mode) {
      schedulerFixture(0x158000, priority);
      installTrap(priority, 3, 0x159000, 0x7800);
      omrState.Enables |= QUEUE_EMPTY_TRAPENB;
      cpuState.running = false;
      expect("empty queue delivers initial trap", pollTimers() != 0, 0);
      expect("empty queue initial handler", cpuState.wptr, 0x159000);
      expect("empty queue initial return", tret_(0) != 0, 0);
      expect("empty queue notification stays latched", pollTimers() != 0, 0);
      expect("latched empty queue leaves caller unchanged", cpuState.wptr, 0x158000);
      cpuState.areg = priority;
      cpuState.breg = 0x15a000;
      cpuState.creg = 0x15a000;
      if (mode == 0) expect("swapqueue rearms empty notification", swapqueue_(0) != 0, 0);
      else if (mode == 1) expect("insertqueue rearms empty notification", insertqueue_(0) != 0, 0);
      else {
        cpuState.areg = 0x15a000;
        expect("legacy front store rearms empty notification", (priority == HIGH_PRIORITY ? sthf_(0) : stlf_(0)) != 0, 0);
        expect("legacy back store completes list", (priority == HIGH_PRIORITY ? sthb_(0) : stlb_(0)) != 0, 0);
      }
      cpuState.areg = priority;
      cpuState.breg = NO_PROCESS;
      cpuState.creg = NO_PROCESS;
      expect("detach queue before next dispatch", swapqueue_(0) != 0, 0);
      expect("rearmed empty queue delivers again", pollTimers() != 0, 0);
      expect("rearmed empty queue handler", cpuState.wptr, 0x159000);
      expect("rearmed empty queue return", tret_(0) != 0, 0);
    }
  }
  schedulerFixture(0x20000);
}

static void testTrapEntryReturn() {
  for (unsigned char priority : {static_cast<unsigned char>(HIGH_PRIORITY), static_cast<unsigned char>(LOW_PRIORITY)}) {
    for (uint32_t cause : {1U, 2U, 4U, 8U, 16U, 32U}) {
      schedulerFixture(0xc0000, priority);
      uint32_t group = cause == 1 ? 0 : cause <= 4 ? 1 : 2;
      installTrap(priority, group, 0xc1000, 0x5000);
      omrState.Enables |= cause;
      cpuState.status = 0x100000;
      cpuState.areg = 0x1234;
      cpuState.breg = 0x5678;
      cpuState.creg = 0x9abc;
      expect("trap entry succeeds", requestTrap(cause, priority) != 0, 0);
      expect("trap handler Iptr", cpuState.iptr, 0x5000);
      expect("trap handler Wptr", cpuState.wptr, 0xc1000);
      expect("trap entry preserves A", cpuState.areg, 0x1234);
      expect("trap entry preserves B", cpuState.breg, 0x5678);
      expect("trap entry preserves C", cpuState.creg, 0x9abc);
      uint32_t saved = 0;
      uint32_t record = trapRecordAddress(priority, group, true);
      readBytes(record + 4, 4, &saved);
      expect("trap record contains cause", saved, 0x100000U | cause);
      readBytes(record + 8, 4, &saved);
      expect("trap record caller Wptr", saved, 0xc0000);
      readBytes(record + 12, 4, &saved);
      expect("trap record caller Iptr", saved, 0x3000);
      expect("trap mask applied", trapEnableMask(priority), 0);
      expect("trap handler cannot suspend", suspendProcess() == TRAP_HANDLER_BLOCKED, 1);
      cpuState.areg = 99;
      expect("tret succeeds", tret_(0) != 0, 0);
      expect("tret restores caller Wptr", cpuState.wptr, 0xc0000);
      expect("tret restores caller Iptr", cpuState.iptr, 0x3000);
      expect("tret clears cause", cpuState.status, 0x100000);
      expect("tret restores mask", trapEnableMask(priority), cause);
      expect("tret does not magically restore A", cpuState.areg, 99);
    }
  }
  schedulerFixture(0xc0000);
  installTrap(LOW_PRIORITY, 0, 0xc1000, 0x5000, 0xffffffff);
  omrState.Enables |= BREAKPOINT_TRAPENB;
  requestTrap(BREAKPOINT_TRAPENB, LOW_PRIORITY);
  expect("same-group reentry rejected", requestTrap(BREAKPOINT_TRAPENB, LOW_PRIORITY) == TRAP_REENTRY, 1);
  tret_(0);
  schedulerFixture(0x20000);
}

static void testCapturedInvalidReturn() {
  setPortableInstructionAbi(false);
  for (bool fallback : {false, true}) {
    schedulerFixture(0x290000, LOW_PRIORITY);
    cpuState.interruptsDisabled = true;
    installTrap(HIGH_PRIORITY, TRAP_GROUP_SCHEDULER, 0x291000, 0xab00);
    uint32_t previous = 0;
    changeTrapMask(HIGH_PRIORITY, RUN_TRAPENB, true, &previous);
    cpuState.areg = 0x292000;
    expect("invalid captured return enters HP handler", runp_(0) != 0, 0);
    const uint32_t invalid[] = {0x00ff0000, STATUS_TIMESLICE_ENABLE,
        0x293001, 0x6f000000, 0x11, 0x22, 0x33};
    for (uint32_t index = 0; index < 7; ++index) storeBytes(0x294000 + index * 4, 4, invalid[index]);
    cpuState.areg = 7;
    cpuState.breg = LOW_PRIORITY;
    cpuState.creg = 0x294000;
    expect("invalid captured return loads invalid bank", ldshadow_(0) != 0, 0);
    storeBytes(0x294004, 4, STATUS_VALID | STATUS_TIMESLICE_ENABLE);
    cpuState.areg = 2;
    cpuState.breg = LOW_PRIORITY;
    cpuState.creg = 0x294000;
    expect("invalid captured return rejects unaligned activation", ldshadow_(0) == INVALID_SHADOW_ACCESS, 1);
    if (fallback) {
      storeBytes(0x295000 - 4, 4, 0xac00);
      expect("invalid captured return queues fallback", enqueueProcess(0x295000, LOW_PRIORITY) != 0, 0);
    }
    expect("invalid captured return leaves handler", tret_(0) != 0, 0);
    expect("invalid captured caller not revived", cpuState.running, 0);
    expect("invalid captured return clears interruption disable", cpuState.interruptsDisabled, 0);
    if (fallback) {
      expect("invalid captured return schedules fallback", pollTimers() != 0, 0);
      expect("invalid captured fallback running", cpuState.running, 1);
      expect("invalid captured fallback workspace", cpuState.wptr, 0x295000);
      expect("invalid captured fallback Iptr", cpuState.iptr, 0xac00);
      expect("invalid captured fallback consumed once", omrState.ProcQueueFPtr[LOW_PRIORITY], NO_PROCESS);
    } else {
      expect("invalid captured return does not fetch stale Iptr", decodeNextInstr() == CPU_IDLE, 1);
    }
  }
  schedulerFixture(0x20000);
}

static void testTrapEnableEntryRules() {
  const uint32_t globals[] = {0, 0x1f, 0xff, 0x8000, 0xaa55, 0x55aa, 0xffff};
  for (bool portable : {false, true}) {
    setPortableInstructionAbi(portable);
    for (unsigned char priority : {static_cast<unsigned char>(HIGH_PRIORITY), static_cast<unsigned char>(LOW_PRIORITY)}) {
      for (uint32_t callerValue : globals) {
        for (uint32_t handlerValue : globals) {
          uint32_t callerGlobal = callerValue & (portable ? 0xffU : 0xffffU);
          uint32_t handlerGlobal = handlerValue & (portable ? 0xffU : 0xffffU);
          constexpr uint32_t callerTraps = BREAKPOINT_TRAPENB | INTEGER_ERR_TRAPENB | RUN_TRAPENB;
          constexpr uint32_t handlerTraps = INTEGER_ERR_TRAPENB | SIGNAL_TRAPENB;
          schedulerFixture(0x280000, priority);
          installTrap(priority, 0, 0x281000, 0xa980, (handlerGlobal << 16) | handlerTraps);
          omrState.Enables = (callerGlobal << 16) | callerTraps;
          expect("trap enable rule enters handler", requestTrap(BREAKPOINT_TRAPENB, priority) != 0, 0);
          expect("trap global enable rule", omrState.Enables >> 16,
              portable ? callerGlobal & handlerGlobal : handlerGlobal);
          expect("trap trap-enable intersection", trapEnableMask(priority), callerTraps & handlerTraps);
          uint32_t saved = 0;
          readBytes(trapRecordAddress(priority, 0, true), 4, &saved);
          expect("trap saved original enable word", saved, (callerGlobal << 16) | callerTraps);
          expect("trap enable rule returns", tret_(0) != 0, 0);
          expect("trap return restores global enables", omrState.Enables >> 16, callerGlobal);
          expect("trap return restores trap enables", trapEnableMask(priority), callerTraps);
        }
      }
    }
  }
  setPortableInstructionAbi(false);
  schedulerFixture(0x20000);
}

static void testTrapInstructionHooks() {
  struct CheckCase { int (*handler)(uint32_t); uint32_t areg; uint32_t breg; uint32_t creg; unsigned int pops; };
  const CheckCase checkCases[] = {{cb_, 128, 0x1234, 0x5678, 0},
      {cbu_, 256, 0x1234, 0x5678, 0}, {cs_, 32768, 0x1234, 0x5678, 0},
      {csu_, 65536, 0x1234, 0x5678, 0}, {csngl_, 0x80000000, 0, 0x5678, 0},
      {cflerr_, 0x7f800001, 0x1234, 0x5678, 0}, {ccnt1_, 10, 0, 0x5678, 1},
      {csub0_, 10, 10, 0x5678, 1}, {cword_, 128, 128, 0x5678, 1},
      {cir_, 0xffffffff, 1, 2, 2}, {ciru_, 1, 2, 0, 2}};
  for (unsigned char priority : {static_cast<unsigned char>(HIGH_PRIORITY), static_cast<unsigned char>(LOW_PRIORITY)}) {
    for (const CheckCase &entry : checkCases) {
      schedulerFixture(0xc2000, priority);
      installTrap(priority, 1, 0xc3000, 0x5100);
      omrState.Enables |= INTEGER_ERR_TRAPENB;
      cpuState.areg = entry.areg;
      cpuState.breg = entry.breg;
      cpuState.creg = entry.creg;
      expect("check delivers integer error trap", entry.handler(0) != 0, 0);
      expect("check error handler selected", cpuState.wptr, 0xc3000);
            uint32_t savedStatus = 0;
            expect("check trapped status readable",
              readBytes(trapRecordAddress(priority, 1, true) + 4, 4, &savedStatus) != 0, 0);
            expect("check trap cause", savedStatus & 7U, INTEGER_ERR_TRAPENB);
      expect("check trap A stack effect", cpuState.areg,
             entry.pops == 0 ? entry.areg : entry.pops == 1 ? entry.breg : entry.creg);
      expect("check trap B stack effect", cpuState.breg,
              entry.handler == csngl_ ? entry.creg : entry.pops == 0 ? entry.breg : entry.pops == 1 ? entry.creg : undefinedWord);
      expect("check trap C stack effect", cpuState.creg,
              entry.handler == csngl_ ? undefinedWord : entry.pops == 0 ? entry.creg : undefinedWord);
      expect("check trap no fallback flag", omrState.HP_ErrorFlag | omrState.LP_ErrorFlag, 0);
      expect("check trap return", tret_(0) != 0, 0);
      schedulerFixture(0xc2000, priority);
      cpuState.areg = entry.areg;
      cpuState.breg = entry.breg;
      cpuState.creg = entry.creg;
      omrState.Enables |= INTEGER_OVF_TRAPENB;
      omrState.HaltOnErrorFlag = 1;
      expect("check masked error halts instead of overflow trap", entry.handler(0) == CPU_HALTED, 1);
      expect("check error halt latched", omrState.halted, 1);
    }
  }
  schedulerFixture(0xc2000);
  installTrap(LOW_PRIORITY, 1, 0xc3000, 0x5100);
  omrState.Enables |= INTEGER_OVF_TRAPENB;
  cpuState.areg = 1;
  cpuState.breg = 0x7fffffff;
  expect("checked add delivers overflow trap", add_(0) != 0, 0);
  expect("overflow handler selected", cpuState.wptr, 0xc3000);
  expect("overflow keeps wrapped result available", cpuState.areg, 0x80000000);
  expect("trapped arithmetic does not set fallback flag", omrState.LP_ErrorFlag, 0);
  tret_(0);
  schedulerFixture(0xc2000);
  cpuState.areg = 1;
  cpuState.breg = 0x7fffffff;
  add_(0);
  expect("disabled overflow trap sets current error flag", omrState.LP_ErrorFlag, 1);
  cpuState.breg = 0x1234;
  cpuState.creg = 0x5678;
  testerr_(0);
  expect("testerr reports and clears error", cpuState.areg, 0);
  expect("testerr clears flag", omrState.LP_ErrorFlag, 0);
  expect("testerr pushes over old A", cpuState.breg, 0x80000000);
  expect("testerr pushes old B into C", cpuState.creg, 0x1234);
  testerr_(0);
  expect("testerr no-error result", cpuState.areg, 1);
  omrState.HaltOnErrorFlag = 1;
  cpuState.areg = 0;
  cpuState.breg = 1;
  expect("division error halts when configured", div_(0) == CPU_HALTED, 1);
  expect("halt latched", omrState.halted, 1);
  expect("halt stops fetch", decodeNextInstr() == CPU_HALTED, 1);

  schedulerFixture(0xc4000);
  installTrap(LOW_PRIORITY, 2, 0xc5000, 0x5200);
  omrState.Enables |= LOADTRAP_TRAPENB;
  cpuState.areg = 0;
  cpuState.breg = 0xc6000;
  cpuState.creg = HIGH_PRIORITY;
  expect("descriptor load intercepted", ldtraph_(0) != 0, 0);
  expect("monitoring handler preserves group argument", cpuState.areg, 0);
  expect("monitoring handler preserves buffer argument", cpuState.breg, 0xc6000);
  expect("load monitoring system handler selected", cpuState.wptr, 0xc5000);
  tret_(0);

  schedulerFixture(0xc7000);
  installTrap(LOW_PRIORITY, 0, 0xc8000, 0x5300);
  omrState.Enables |= BREAKPOINT_TRAPENB;
  cpuState.iptr = 0x3800;
  storeBytes(0x3800, 1, 0);
  decodeNextInstr();
  int watchFlag = 0;
  expect("encoded breakpoint enters handler", execInstr(&watchFlag) != 0, 0);
  uint32_t saved = 0;
  readBytes(trapRecordAddress(LOW_PRIORITY, 0, true) + 12, 4, &saved);
  expect("breakpoint return points after instruction", saved, 0x3801);
  tret_(0);
  schedulerFixture(0x20000);
}

static void testSchedulerTrapDelivery() {
  uint32_t previous = 0;
  schedulerFixture(0xc9000);
  installTrap(LOW_PRIORITY, 3, 0xca000, 0x5400);
  omrState.Enables |= TIMER_TRAPENB;
  cpuState.areg = 0;
  tin_(0);
  expect("timer expiry enters handler", advanceTimers(3888) != 0, 0);
  expect("timer handler runs while caller was idle", cpuState.wptr, 0xca000);
  expect("timer handler receives waiting descriptor", cpuState.breg, 0xc9001);
  expect("timer waiter detached for software scheduling", omrState.TptrReg[LOW_PRIORITY], NO_PROCESS);
  cpuState.areg = cpuState.breg;
  runp_(0);
  tret_(0);
  pollTimers();
  expect("timer kernel can schedule waiter", cpuState.wptr, 0xc9000);

  schedulerFixture(0xcb000);
  installTrap(HIGH_PRIORITY, 3, 0xcc000, 0x5500);
  changeTrapMask(HIGH_PRIORITY, RUN_TRAPENB, true, &previous);
  storeBytes(0xcd000 - 4, 4, 0x5600);
  cpuState.areg = 0xcd000;
  expect("run trap crosses to selected priority", runp_(0) != 0, 0);
  expect("run kernel priority", cpuState.priority, HIGH_PRIORITY);
  expect("run kernel receives process", cpuState.breg, 0xcd000);
  expect("run trap does not pre-enqueue process", omrState.ProcQueueFPtr[HIGH_PRIORITY], NO_PROCESS);
  cpuState.areg = cpuState.breg;
  runp_(0);
  tret_(0);
  expect("scheduler trap restores caller priority", cpuState.priority, LOW_PRIORITY);
  expect("scheduler trap restores caller workspace", cpuState.wptr, 0xcb000);
  pollTimers();
  expect("kernel scheduled high process runs", cpuState.wptr, 0xcd000);
  stopp_(0);

  schedulerFixture(0xce000);
  installTrap(HIGH_PRIORITY, 3, 0xcf000, 0x5700);
  changeTrapMask(HIGH_PRIORITY, PROCESS_TRAPENB, true, &previous);
  storeBytes(0xd0000 - 4, 4, 0x5800);
  enqueueProcess(0xd0000, HIGH_PRIORITY);
  pollTimers();
  expect("priority interrupt delivered", cpuState.wptr, 0xcf000);
  tret_(0);
  expect("priority interrupt returns to high process", cpuState.wptr, 0xd0000);
  stopp_(0);

  schedulerFixture(0xd1000);
  installTrap(LOW_PRIORITY, 3, 0xd2000, 0x5900);
  storeBytes(0xd2000, 4, 0x12345678);
  omrState.Enables |= QUEUE_EMPTY_TRAPENB;
  stopp_(0);
  expect("queue empty trap delivered", cpuState.wptr, 0xd2000);
  uint32_t parameter = 0;
  expect("queue empty parameter slot readable", readBytes(0xd2000, 4, &parameter) != 0, 0);
  expect("descriptor-less trap preserves parameter slot", parameter, 0x12345678);
  tret_(0);
  expect("queue empty returns to idle", cpuState.running, 0);
  expect("queue empty is not repeatedly delivered", decodeNextInstr() == CPU_IDLE, 1);

  schedulerFixture(0xc04307b0);
  installTrap(LOW_PRIORITY, TRAP_GROUP_SCHEDULER, 0xc042a5e0, 0xc042a004, 0);
  storeBytes(trapRecordAddress(LOW_PRIORITY, TRAP_GROUP_SCHEDULER, false) + 4, 4, 0x80000000);
  storeBytes(0xc042a5e0, 4, 0);
  storeBytes(0xc04047d4, 4, 0x80000140);
  uint32_t callerStatus = cpuState.status;
  uint32_t callerIptr = cpuState.iptr;
  omrState.Enables |= RUN_TRAPENB;
  cpuState.areg = 0xc04047ed;
  expect("firmware run trap enters handler", runp_(0) != 0, 0);
  expect("firmware handler preserves installed status", cpuState.status, 0x80010000);
  expect("firmware run trap retains B descriptor", cpuState.breg, 0xc04047ed);
  expect("firmware handler adjusts workspace", ajw_(static_cast<uint32_t>(-6)) != 0, 0);
  expect("firmware handler loads scheduler parameter", ldl_(6) != 0, 0);
  expect("firmware handler receives descriptor in entry workspace", cpuState.areg, 0xc04047ed);
  ldc_(0xfffffffc);
  and_(0);
  expect("firmware handler loads process metadata", ldnl_(static_cast<uint32_t>(-6)) != 0, 0);
  expect("firmware handler receives control block", cpuState.areg, 0x80000140);
  cpuState.priority = HIGH_PRIORITY;
  expect("firmware trap rejects return from wrong priority", tret_(0) == INVALID_TRAP_RETURN, 1);
  cpuState.priority = LOW_PRIORITY;
  expect("firmware run trap returns", tret_(0) != 0, 0);
  expect("firmware trap restores caller workspace", cpuState.wptr, 0xc04307b0);
  expect("firmware trap restores caller instruction", cpuState.iptr, callerIptr);
  expect("firmware trap restores caller status without temporary return priority", cpuState.status, callerStatus);
  expect("firmware trap frame consumed", trapHandlerActive(), 0);

  schedulerFixture(0xc04307c4);
  installTrap(LOW_PRIORITY, TRAP_GROUP_SCHEDULER, 0xc042a5e0, 0xc042a004, 0);
  storeBytes(trapRecordAddress(LOW_PRIORITY, TRAP_GROUP_SCHEDULER, false) + 4, 4, 0x80000000);
  omrState.Enables |= QUEUE_EMPTY_TRAPENB;
  expect("firmware queue empty enters handler", stopp_(0) != 0, 0);
  expect("firmware queue empty handler return priority", cpuState.status, 0x80010000);
  storeBytes(0x80000140, 4, 0x80100000);
  storeBytes(0x80000144, 4, 0xc04047ec);
  storeBytes(0x80000148, 4, 0xc0401832);
  cpuState.areg = 12;
  cpuState.breg = trapRecordAddress(LOW_PRIORITY, TRAP_GROUP_SCHEDULER, true) + 4;
  cpuState.creg = 0x80000140;
  expect("firmware scheduler replaces trapped task context", move_(0) != 0, 0);
  for (uint32_t returnPriority : {0U, 2U, 3U}) {
    cpuState.status = 0x80000000U | (returnPriority << 16);
    CPUSTATE before = cpuState;
    uint32_t enablesBefore = omrState.Enables;
    expect("invalid scheduler return priority rejected", tret_(0) == INVALID_TRAP_RETURN, 1);
    expect("rejected scheduler return preserves Status", cpuState.status, before.status);
    expect("rejected scheduler return preserves Wptr", cpuState.wptr, before.wptr);
    expect("rejected scheduler return preserves Iptr", cpuState.iptr, before.iptr);
    expect("rejected scheduler return preserves A", cpuState.areg, before.areg);
    expect("rejected scheduler return preserves B", cpuState.breg, before.breg);
    expect("rejected scheduler return preserves C", cpuState.creg, before.creg);
    expect("rejected scheduler return preserves priority", cpuState.priority, before.priority);
    expect("rejected scheduler return preserves enables", omrState.Enables, enablesBefore);
    expect("rejected scheduler return preserves trap frame", trapHandlerActive(), 1);
  }
  cpuState.status = 0x80010000;
  expect("firmware scheduler returns to replacement task", tret_(0) != 0, 0);
  expect("replacement task uses handler return priority", cpuState.priority, LOW_PRIORITY);
  expect("replacement task restores native Status", cpuState.status, 0x80100000);
  expect("replacement task restores Wptr", cpuState.wptr, 0xc04047ec);
  expect("replacement task restores Iptr", cpuState.iptr, 0xc0401832);
  expect("replacement task is running", cpuState.running, 1);
  expect("replacement task return consumes trap frame", trapHandlerActive(), 0);
  storeBytes(0xc07ffb7c, 4, 0);
  storeBytes(0xc07ffb80, 4, NO_PROCESS);
  storeBytes(0xc07ffb84, 4, NO_PROCESS);
  cpuState.areg = 0xc07ffb7c;
  expect("replacement task blocks on semaphore", wait_(0) != 0, 0);
  expect("blocking replacement task rearms queue empty trap", trapHandlerActive(), 1);
  expect("second queue empty enters scheduler workspace", cpuState.wptr, 0xc042a5e0);
  expect("second queue empty enters scheduler instruction", cpuState.iptr, 0xc042a004);
  uint32_t semaphoreFront = 0;
  expect("replacement task semaphore readable", readBytes(0xc07ffb80, 4, &semaphoreFront) != 0, 0);
  expect("replacement task queued on semaphore", semaphoreFront, 0xc04047ed);
  expect("second queue empty returns to idle", tret_(0) != 0, 0);
  expect("second queue empty leaves CPU idle", cpuState.running, 0);
  expect("idle trap return does not retrigger queue empty", decodeNextInstr() == CPU_IDLE, 1);
  expect("idle trap return leaves no active handler", trapHandlerActive(), 0);
  for (unsigned char priority : {static_cast<unsigned char>(HIGH_PRIORITY),
                                 static_cast<unsigned char>(LOW_PRIORITY)}) {
    schedulerFixture(0xc04307c0);
    cpuState.priority = priority;
    cpuState.iptr = 0xc040284a;
    installTrap(priority, TRAP_GROUP_SCHEDULER, 0xc042a5e0, 0xc042a004, 0);
    storeBytes(trapRecordAddress(priority, TRAP_GROUP_SCHEDULER, false) + 4, 4, STATUS_VALID);
    omrState.Enables |= QUEUE_EMPTY_TRAPENB;
    expect("same-pointer context enters scheduler", stopp_(0) != 0, 0);
    uint32_t trapped = trapRecordAddress(priority, TRAP_GROUP_SCHEDULER, true);
    storeBytes(0xc042a614, 4, STATUS_VALID | STATUS_TIMESLICE_ENABLE);
    storeBytes(0xc042a618, 4, 0xc04307c0);
    storeBytes(0xc042a61c, 4, 0xc040284a);
    cpuState.areg = 12;
    cpuState.breg = trapped + 4;
    cpuState.creg = 0xc042a614;
    expect("same-pointer context copied to trapped record", move_(0) != 0, 0);
    expect("same-pointer valid context returns", tret_(0) != 0, 0);
    expect("same-pointer valid context restores Status", cpuState.status, STATUS_VALID | STATUS_TIMESLICE_ENABLE);
    expect("same-pointer valid context restores workspace", cpuState.wptr, 0xc04307c0);
    expect("same-pointer valid context restores instruction", cpuState.iptr, 0xc040284a);
    expect("same-pointer valid context restores priority", cpuState.priority, priority);
    expect("same-pointer valid context resumes", cpuState.running, 1);
    storeBytes(0xc040284a, 1, 0x40);
    expect("same-pointer valid context can fetch", decodeNextInstr() != 0, 0);
    expect("resumed context stops again", stopp_(0) != 0, 0);
    uint32_t savedStatus = 0;
    expect("stopped context status readable", readBytes(trapped + 4, 4, &savedStatus) != 0, 0);
    expect("stopped context is no longer valid", savedStatus & STATUS_VALID, 0);
    expect("stopped context trap returns", tret_(0) != 0, 0);
    expect("stopped context remains idle", cpuState.running, 0);
    expect("stopped context fetch remains idle", decodeNextInstr() == CPU_IDLE, 1);
  }
  schedulerFixture(0x20000);
}

static void testTrapSnapshot() {
  char directory[] = "/tmp/st20-trap-snapshot-XXXXXX";
  if (!mkdtemp(directory)) {
    expect("trap snapshot directory", 0, 1);
    return;
  }
  schedulerFixture(0xe0000);
  installTrap(LOW_PRIORITY, 1, 0xe1000, 0x6000);
  installTrap(LOW_PRIORITY, 3, 0xe3000, 0x6100);
  uint32_t previous = 0;
  changeTrapMask(HIGH_PRIORITY, ILL_OPCODE_TRAPENB, true, &previous);
  omrState.Enables |= INTEGER_OVF_TRAPENB;
  cpuState.status = 0x2000000;
  cpuState.areg = 1;
  cpuState.breg = 0x7fffffff;
  add_(0);
  changeTrapMask(LOW_PRIORITY, TIMER_TRAPENB, true, &previous);
  storeBytes(0xe2000 - 4, 4, 0x6200);
  storeBytes(0xe2000 - 16, 4, NO_PROCESS);
  storeBytes(0xe2000 - 20, 4, 1);
  swapTimerList(LOW_PRIORITY, 0xe2000, &previous);
  advanceTimers(3888);
  expect("pending timer does not replace active error handler", cpuState.wptr, 0xe1000);
  uint32_t enables = 0;
  uint32_t saved = trapRecordAddress(LOW_PRIORITY, 1, true);
  readBytes(saved, 4, &enables);
  storeBytes(saved, 4, static_cast<uint32_t>(enables | TIMER_TRAPENB));
  expect("snapshot inside trap saves", saveCPUState(directory) != 0, 0);
  resetScheduler();
  omrState = {};
  expect("snapshot inside trap loads", loadCPUState(directory) != 0, 0);
  expect("snapshot restores active trap", trapHandlerActive(), 1);
  expect("snapshot restores Status", cpuState.status, 1U << 18);
  expect("snapshot restores inactive priority trap mask", trapEnableMask(HIGH_PRIORITY), ILL_OPCODE_TRAPENB);
  expect("restored handler can return", tret_(0) != 0, 0);
  expect("restored trap returns caller workspace", cpuState.wptr, 0xe0000);
  expect("restored trap returns caller Status", cpuState.status, 0x2000000);
  pollTimers();
  expect("snapshot retains pending timer trap", cpuState.wptr, 0xe3000);
  expect("snapshot pending timer descriptor", cpuState.breg, 0xe2001);
  cpuState.areg = cpuState.breg;
  runp_(0);
  tret_(0);
  std::string filename = std::string(directory) + "/cpu.bin";
  std::remove(filename.c_str());
  rmdir(directory);
  schedulerFixture(0x20000);
}

static void testGeneratedAndNestedTraps() {
  for (uint32_t bit = 0; bit < 14; ++bit) {
    schedulerFixture(0xe4000);
    uint32_t cause = 1U << bit;
    uint32_t group = bit == 0 ? 0U : bit <= 2 ? 1U : bit <= 5 ? 2U : 3U;
    installTrap(LOW_PRIORITY, group, 0xe5000, 0x6300);
    omrState.Enables |= cause;
    cpuState.areg = cause;
    cpuState.breg = 0xe4001;
    expect("generated trap cause delivered", causeerror_(0) != 0, 0);
    uint32_t status = 0;
    readBytes(trapRecordAddress(LOW_PRIORITY, group, true) + 4, 4, &status);
    expect("generated trap records causeerror flag", (status & 0x8000) != 0, 1);
    expect("generated trap records requested cause", (status & cause) != 0, 1);
    expect("generated trap can return", tret_(0) != 0, 0);
    expect("generated flag cleared on return", cpuState.status & 0x8000, 0);
  }
  schedulerFixture(0xe6000);
  installTrap(LOW_PRIORITY, 0, 0xe7000, 0x6400, 0xfffffffe);
  installTrap(LOW_PRIORITY, 2, 0xe8000, 0x6500);
  omrState.Enables |= BREAKPOINT_TRAPENB | ILL_OPCODE_TRAPENB;
  requestTrap(BREAKPOINT_TRAPENB, LOW_PRIORITY);
  requestTrap(ILL_OPCODE_TRAPENB, LOW_PRIORITY);
  expect("different trap group may nest", cpuState.wptr, 0xe8000);
  tret_(0);
  expect("nested trap returns outer handler", cpuState.wptr, 0xe7000);
  storeBytes(0xe9000, 4, 0);
  storeBytes(0xe9004, 4, NO_PROCESS);
  cpuState.areg = 0xe9000;
  expect("handler wait blocked before queue change", wait_(0) == TRAP_HANDLER_BLOCKED, 1);
  uint32_t value = 0;
  readBytes(0xe9004, 4, &value);
  expect("blocked handler leaves semaphore untouched", value, NO_PROCESS);
  cpuState.areg = 1;
  cpuState.breg = 0xe9004;
  cpuState.creg = 0;
  expect("handler channel block rejected", in_(0) == TRAP_HANDLER_BLOCKED, 1);
  readBytes(0xe9004, 4, &value);
  expect("blocked handler leaves channel untouched", value, NO_PROCESS);
  cpuState.areg = 10;
  expect("handler timer block rejected", tin_(0) == TRAP_HANDLER_BLOCKED, 1);
  expect("blocked handler leaves timer list untouched", omrState.TptrReg[LOW_PRIORITY], NO_PROCESS);
  tret_(0);
  expect("outer trap returns original caller", cpuState.wptr, 0xe6000);
  schedulerFixture(0x20000);
  cpuState.areg = 3;
  cpuState.breg = 0x1234;
  cpuState.creg = 3;
  ldiv_(0);
  expect("ldiv overflow leaves A unchanged", cpuState.areg, 3);
  expect("ldiv overflow leaves B unchanged", cpuState.breg, 0x1234);
  cpuState.areg = 0xffffffff;
  cpuState.breg = 0x80000000;
  cpuState.creg = 0;
  rem_(0);
  expect("remainder minimum divided by minus one is safe", cpuState.areg, 0);
  schedulerFixture(0x20000);
}

static void testSignalAndChannelDelivery() {
  uint32_t previous = 0;
  schedulerFixture(0xf0000);
  installTrap(LOW_PRIORITY, 3, 0xf1000, 0x6600);
  storeBytes(0xf2000, 4, 0);
  storeBytes(0xf2004, 4, NO_PROCESS);
  cpuState.areg = 0xf2000;
  wait_(0);
  cpuState.running = true;
  cpuState.wptr = 0xf3000;
  cpuState.areg = 0xf2000;
  changeTrapMask(LOW_PRIORITY, SIGNAL_TRAPENB, true, &previous);
  expect("signal trap delivers waiter to kernel", signal_(0) != 0, 0);
  expect("signal handler selected", cpuState.wptr, 0xf1000);
  expect("signal handler waiter parameter", cpuState.breg, 0xf0001);
  uint32_t word = 0;
  readBytes(0xf2004, 4, &word);
  expect("signal trap detaches waiter", word, NO_PROCESS);
  expect("signal kernel owns enqueue decision", omrState.ProcQueueFPtr[LOW_PRIORITY], NO_PROCESS);
  cpuState.areg = cpuState.breg;
  runp_(0);
  tret_(0);
  stopp_(0);
  expect("signal kernel can reschedule waiter", cpuState.wptr, 0xf0000);

  schedulerFixture(0xf4000);
  installTrap(LOW_PRIORITY, 3, 0xf5000, 0x6700);
  storeBytes(0xf6000, 4, NO_PROCESS);
  cpuState.areg = 4;
  cpuState.breg = 0xf6000;
  cpuState.creg = 0xf7000;
  in_(0);
  changeTrapMask(LOW_PRIORITY, INTERNALCH_TRAPENB, true, &previous);
  cpuState.running = true;
  cpuState.priority = HIGH_PRIORITY;
  cpuState.wptr = 0xf8000;
  changeTrapMask(HIGH_PRIORITY, 0x3fff, false, &previous);
  cpuState.areg = 0x11223344;
  cpuState.breg = 0xf6000;
  expect("channel trap delivers completed communication", outword_(0) != 0, 0);
  expect("channel handler priority", cpuState.priority, LOW_PRIORITY);
  expect("channel handler peer descriptor", cpuState.breg, 0xf4001);
  readBytes(0xf7000, 4, &word);
  expect("channel data copied before scheduling trap", word, 0x11223344);
  readBytes(0xf6000, 4, &word);
  expect("channel reset before scheduling trap", word, NO_PROCESS);
  cpuState.areg = cpuState.breg;
  runp_(0);
  tret_(0);
  expect("cross-priority channel trap returns sender", cpuState.wptr, 0xf8000);
  expect("channel trap restores sender priority", cpuState.priority, HIGH_PRIORITY);
  stopp_(0);
  expect("channel kernel rescheduled receiver", cpuState.wptr, 0xf4000);
  schedulerFixture(0x20000);
}

static void testVersion2AndHaltSnapshot() {
  char directory[] = "/tmp/st20-v2-snapshot-XXXXXX";
  if (!mkdtemp(directory)) {
    expect("v2 snapshot directory", 0, 1);
    return;
  }
  schedulerFixture(0xf9000);
  cpuState.move2dBlockLength = 3;
  cpuState.move2dDestStride = 9;
  cpuState.move2dSourceStride = 7;
  saveCPUState(directory);
  std::string filename = std::string(directory) + "/cpu.bin";
  FILE *file = std::fopen(filename.c_str(), "r+b");
  if (file) {
    std::fseek(file, 0, SEEK_END);
    int64_t prefix = std::ftell(file) - static_cast<int64_t>(cpuRuntimeSnapshotSize());
    std::fseek(file, prefix + static_cast<uint32_t>(sizeof(uint32_t)), SEEK_SET);
    uint32_t version = 2;
    std::fwrite(&version, sizeof(version), 1, file);
    std::fflush(file);
    ftruncate(fileno(file), prefix + 39 * static_cast<uint32_t>(sizeof(uint32_t)));
    std::fclose(file);
    resetScheduler();
    cpuState.move2dBlockLength = 4;
    expect("reject version-2 snapshot", loadCPUState(directory) == INVALID_CPU_FILE, 1);
    expect("version-2 rejection preserves block parameters", cpuState.move2dBlockLength, 4);
    expect("version-2 rejection preserves inactive trap state", trapHandlerActive(), 0);
  } else expect("v2 fixture open", 0, 1);
  omrState.HaltOnErrorFlag = 1;
  seterr_(0);
  saveCPUState(directory);
  resetScheduler();
  omrState = {};
  loadCPUState(directory);
  expect("halt latch persists in snapshot", omrState.halted, 1);
  expect("loaded halted processor cannot fetch", decodeNextInstr() == CPU_HALTED, 1);
  uint32_t stoppedWorkspace = cpuState.wptr;
  storeBytes(0xfa000 - 4, 4, 0x6800);
  enqueueProcess(0xfa000, HIGH_PRIORITY);
  expect("halted processor cannot dispatch ready work", pollTimers() == CPU_HALTED, 1);
  expect("halted processor retains faulting workspace", cpuState.wptr, stoppedWorkspace);
  expect("halted processor does not enter generated trap", requestTrap(BREAKPOINT_TRAPENB, LOW_PRIORITY) == CPU_HALTED, 1);
  std::remove(filename.c_str());
  rmdir(directory);
  schedulerFixture(0x20000);
}

static void testUntimedAlt() {
  schedulerFixture(0x110000);
  cpuState.areg = 1;
  cpuState.breg = 2;
  cpuState.creg = 3;
  alt_(0);
  expect("alt leaves registers unchanged", cpuState.areg, 1);
  enbs_(0);
  expect("skip guard does not consume registers", cpuState.breg, 2);
  storeBytes(0x110000, 4, 0x76543210);
  altwt_(0);
  expect("ready skip avoids suspension", cpuState.running, 1);
  expect("ready altwt undefines A", cpuState.areg, undefinedWord);
  expect("ready altwt undefines B", cpuState.breg, undefinedWord);
  expect("ready altwt undefines C", cpuState.creg, undefinedWord);
  uint32_t temporary = 0;
  readBytes(0x110000, 4, &temporary);
  expect("ready altwt resets Temp", temporary, ALT_NONE_SELECTED);
  cpuState.areg = 0x20;
  cpuState.breg = 1;
  cpuState.creg = 0x1234;
  diss_(0);
  expect("first ready skip selected", cpuState.areg, 1);
  expect("diss preserves old C in B", cpuState.breg, 0x1234);
  cpuState.areg = 0x40;
  cpuState.breg = 1;
  diss_(0);
  expect("later ready skip not selected", cpuState.areg, 0);
  cpuState.iptr = 0x3000;
  altend_(0);
  expect("altend selected offset", cpuState.iptr, 0x3020);

  schedulerFixture(0x111000);
  storeBytes(0x112000, 4, NO_PROCESS);
  alt_(0);
  cpuState.areg = 1;
  cpuState.breg = 0x112000;
  cpuState.creg = 0x5555;
  enbc_(0);
  expect("enbc preserves guard in A", cpuState.areg, 1);
  expect("enbc shifts C into B", cpuState.breg, 0x5555);
  storeBytes(0x111000, 4, 0x76543210);
  altwt_(0);
  expect("channel ALT blocks", cpuState.running, 0);
  expect("blocking altwt undefines A", cpuState.areg, undefinedWord);
  expect("blocking altwt undefines B", cpuState.breg, undefinedWord);
  expect("blocking altwt undefines C", cpuState.creg, undefinedWord);
  readBytes(0x111000, 4, &temporary);
  expect("blocking altwt resets Temp", temporary, ALT_NONE_SELECTED);
  cpuState.running = true;
  cpuState.wptr = 0x113000;
  cpuState.iptr = 0x4000;
  cpuState.areg = 0x89abcdef;
  cpuState.breg = 0x112000;
  expect("sender notifies waiting ALT", outword_(0) != 0, 0);
  expect("waiting ALT resumes", cpuState.wptr, 0x111000);
  uint32_t value = 0;
  readBytes(0x112000, 4, &value);
  expect("ALT notification retains sender descriptor", value, 0x113001);
  cpuState.areg = 0x30;
  cpuState.breg = 1;
  cpuState.creg = 0x112000;
  disc_(0);
  expect("ready channel guard selects", cpuState.areg, 1);
  cpuState.iptr = 0x3000;
  altend_(0);
  expect("channel branch chosen", cpuState.iptr, 0x3030);
  cpuState.areg = 4;
  cpuState.breg = 0x112000;
  cpuState.creg = 0x114000;
  in_(0);
  readBytes(0x114000, 4, &value);
  expect("selected branch receives message", value, 0x89abcdef);
  expect("sender queued only after input", omrState.ProcQueueFPtr[LOW_PRIORITY], 0x113000);

  schedulerFixture(0x115000);
  storeBytes(0x116000, 4, NO_PROCESS);
  alt_(0);
  cpuState.areg = 1;
  cpuState.breg = 0x116000;
  enbc_(0);
  cpuState.areg = 1;
  enbs_(0);
  altwt_(0);
  cpuState.areg = 0x10;
  cpuState.breg = 1;
  diss_(0);
  cpuState.areg = 0x20;
  cpuState.breg = 1;
  cpuState.creg = 0x116000;
  disc_(0);
  expect("unready channel not selected", cpuState.areg, 0);
  readBytes(0x116000, 4, &value);
  expect("unused guard registration removed", value, NO_PROCESS);
  cpuState.areg = 0;
  cpuState.breg = 0x20000000;
  expect("false channel guard does not access hardware", enbc_(0) != 0, 0);

  schedulerFixture(0x117000);
  storeBytes(0x118000, 4, NO_PROCESS);
  alt_(0);
  cpuState.areg = 1;
  cpuState.breg = 0x118000;
  enbc_(0);
  storeBytes(0x117000, 4, 0x76543210);
  struct ContinuationDevice {
    bool fail{true};
    uint32_t value{0};
    unsigned int writes{0};
  } continuation;
  MemoryDevice saveArea{0x116ffc, 0x116fff, &continuation, nullptr,
      +[](void *context, uint32_t, int, uint32_t value) {
        auto &device = *static_cast<ContinuationDevice *>(context);
        ++device.writes;
        if (device.fail) return WRITE_FAILURE;
        device.value = value;
        return 0;
      }};
  expect("register ALT continuation fault", registerMemoryDevice(saveArea) != 0, 0);
  expect("altwt propagates continuation write failure", altwt_(0) == WRITE_FAILURE, 1);
  expect("failed altwt remains running", cpuState.running, 1);
  readBytes(0x117000 - 12, 4, &temporary);
  expect("failed altwt does not publish Waiting", temporary, ALT_ENABLING);
  readBytes(0x117000, 4, &temporary);
  expect("failed altwt leaves Temp unchanged", temporary, 0x76543210);

  cpuState.wptr = 0x119000;
  cpuState.areg = 0x77;
  cpuState.breg = 0x118000;
  expect("guard can notify after failed altwt", outbyte_(0) != 0, 0);
  expect("notification does not enqueue running ALT owner", omrState.ProcQueueFPtr[LOW_PRIORITY], NO_PROCESS);
  readBytes(0x117000 - 12, 4, &temporary);
  expect("failed-wait owner can become Ready", temporary, ALT_READY);
  schedulerFixture(0x117000);
  expect("ready altwt bypasses failing continuation save", altwt_(0) != 0, 0);
  expect("ready altwt does not attempt Iptr write", continuation.writes, 1);
  readBytes(0x117000, 4, &temporary);
  expect("ready retry initializes Temp", temporary, ALT_NONE_SELECTED);

  alt_(0);
  continuation.fail = false;
  cpuState.iptr = 0x3060;
  expect("altwt can suspend after continuation save recovers", altwt_(0) != 0, 0);
  expect("recovered altwt saves Iptr once", continuation.writes, 2);
  expect("recovered altwt saves continuation", continuation.value, 0x3060);
  expect("recovered altwt suspends process", cpuState.running, 0);
  readBytes(0x117000 - 12, 4, &temporary);
  expect("recovered altwt publishes Waiting", temporary, ALT_WAITING);
  expect("unregister ALT continuation fault", unregisterMemoryDevice(saveArea.start) != 0, 0);
  schedulerFixture(0x20000);
}

static void testTimedAlt() {
  schedulerFixture(0x120000);
  talt_(0);
  cpuState.areg = 1;
  cpuState.breg = 20;
  enbt_(0);
  cpuState.areg = 1;
  cpuState.breg = 10;
  enbt_(0);
  uint32_t value = 0;
  readBytes(0x120000 - 20, 4, &value);
  expect("enbt retains earliest guard", value, 10);
  taltwt_(0);
  expect("timed ALT waits", cpuState.running, 0);
  expect("timed ALT alarm is earliest plus one", omrState.TnextReg[LOW_PRIORITY], 11);
  decodeNextInstr();
  expect("timed ALT wakes at deadline", omrState.ClockRegLP, 11);
  readBytes(0x120000 - 16, 4, &value);
  expect("timer expiry leaves TimeSet marker", value, ALT_TIME_SET);
  cpuState.areg = 0x20;
  cpuState.breg = 1;
  cpuState.creg = 20;
  dist_(0);
  expect("later timer not yet ready", cpuState.areg, 0);
  cpuState.areg = 0x10;
  cpuState.breg = 1;
  cpuState.creg = 10;
  dist_(0);
  expect("earliest timer selected", cpuState.areg, 1);

  schedulerFixture(0x121000);
  storeBytes(0x122000, 4, NO_PROCESS);
  talt_(0);
  cpuState.areg = 1;
  cpuState.breg = 0x122000;
  enbc_(0);
  cpuState.areg = 1;
  cpuState.breg = 30;
  enbt_(0);
  taltwt_(0);
  cpuState.running = true;
  cpuState.wptr = 0x123000;
  cpuState.areg = 0x55;
  cpuState.breg = 0x122000;
  outbyte_(0);
  expect("channel can win timed ALT", cpuState.wptr, 0x121000);
  cpuState.areg = 0x40;
  cpuState.breg = 1;
  cpuState.creg = 0x122000;
  disc_(0);
  cpuState.areg = 0x80;
  cpuState.breg = 1;
  cpuState.creg = 30;
  dist_(0);
  expect("channel winner prevents timer selection", cpuState.areg, 0);
  expect("timed ALT removes unused alarm", omrState.TptrReg[LOW_PRIORITY], NO_PROCESS);
  advanceTimers(31U * 3888U);
  expect("cancelled ALT timer cannot queue duplicate", omrState.ProcQueueFPtr[LOW_PRIORITY], NO_PROCESS);

  schedulerFixture(0x124000);
  omrState.ClockRegLP = 0xfffffffe;
  talt_(0);
  cpuState.areg = 1;
  cpuState.breg = 0xffffffff;
  enbt_(0);
  taltwt_(0);
  advanceTimers(2U * 3888U);
  expect("timed ALT wraps deadline", omrState.ClockRegLP, 0);
  cpuState.areg = 0xfffffff0;
  cpuState.breg = 1;
  cpuState.creg = 0xffffffff;
  dist_(0);
  expect("wrapped timer guard selected", cpuState.areg, 1);
  cpuState.iptr = 0x10;
  altend_(0);
  expect("negative ALT branch wraps", cpuState.iptr, 0);
  schedulerFixture(0x20000);
}

static void testNativeTimedExternalAlt() {
  setPortableInstructionAbi(false);
  constexpr uint32_t channel = 0x8000001c;
  const uint8_t input[] = {0x12, 0x34};
  for (unsigned char priority : {static_cast<unsigned char>(HIGH_PRIORITY), static_cast<unsigned char>(LOW_PRIORITY)}) {
    for (int scenario = 0; scenario < 3; ++scenario) {
      for (bool channelFirst : {false, true}) {
        schedulerFixture(0x270000);
        cpuState.priority = priority;
        omrState.ClockRegHP = omrState.ClockRegLP = scenario == 2 ? 0xfffffffeU : 0U;
        uint32_t time = scenario == 0 ? 4096U : scenario == 1 ? 0U : 0xffffffffU;
        expect("native timed ALT registers FIFO", registerExternalChannel(channel, 8) != 0, 0);
        expect("native timed ALT starts", talt_(0) != 0, 0);
        cpuState.areg = 1;
        cpuState.breg = channel;
        expect("native timed ALT enables FIFO", enbc_(0) != 0, 0);
        cpuState.areg = 1;
        cpuState.breg = time;
        expect("native timed ALT enables timer", enbt_(0) != 0, 0);
        expect("native timed ALT waits", taltwt_(0) != 0, 0);
        expect("native timed ALT suspended", cpuState.running, 0);
        if (scenario != 2)
          expect("native timed ALT channel wakes first", feedExternalChannel(channel, input) != 0, 0);
        expect("native timed ALT advances clocks", advanceTimers(2U * 3888U) != 0, 0);
        if (scenario == 2)
          expect("native timed ALT FIFO ready after timer", feedExternalChannel(channel, input) != 0, 0);
        expect("native timed ALT owner running", cpuState.running, 1);
        expect("native timed ALT owner workspace", cpuState.wptr, 0x270000);
        expect("native timed ALT wakes only once", omrState.ProcQueueFPtr[priority], NO_PROCESS);
        auto disableChannel = [&] {
          cpuState.areg = 0x20;
          cpuState.breg = 1;
          cpuState.creg = channel;
          expect("native timed ALT disables FIFO", disc_(0) != 0, 0);
          return cpuState.areg;
        };
        auto disableTimer = [&] {
          cpuState.areg = 0x10;
          cpuState.breg = 1;
          cpuState.creg = time;
          expect("native timed ALT disables timer", dist_(0) != 0, 0);
          return cpuState.areg;
        };
        if (channelFirst) {
          expect("native timed ALT first FIFO selects", disableChannel(), 1);
          expect("native timed ALT later timer cannot select", disableTimer(), 0);
        } else {
          expect("native timed ALT timer readiness", disableTimer(), scenario == 0 ? 0 : 1);
          expect("native timed ALT subsequent FIFO readiness", disableChannel(), scenario == 0 ? 1 : 0);
        }
        uint32_t offset = 0;
        readBytes(cpuState.wptr, 4, &offset);
        uint32_t expectedOffset = channelFirst || scenario == 0 ? 0x20U : 0x10U;
        expect("native timed ALT selects by disabling order", offset, expectedOffset);
        expect("native timed ALT detaches timer", omrState.TptrReg[priority], NO_PROCESS);
        expect("native timed ALT late clock advance", advanceTimers(uint64_t{4098} * 3888U) != 0, 0);
        expect("native timed ALT cancelled timer never requeues", omrState.ProcQueueFPtr[priority], NO_PROCESS);
        cpuState.areg = 2;
        cpuState.breg = channel;
        cpuState.creg = 0x271000;
        expect("native timed ALT FIFO data retained", in_(0) != 0, 0);
        uint32_t value = 0;
        readBytes(0x271000, 2, &value);
        expect("native timed ALT exact FIFO bytes", value, 0x3412);
        expect("native timed ALT branch executes", altend_(0) != 0, 0);
        expect("native timed ALT branch offset", cpuState.iptr, 0x3000 + expectedOffset);
        expect("native timed ALT has no portable continuation", cpuState.status & (STATUS_PORTABLE_ABI | STATUS_INTERRUPTED_OPERATION_MASK), 0);
        expect("native timed ALT unregisters FIFO", unregisterExternalChannel(channel) != 0, 0);
      }
    }
  }
  schedulerFixture(0x20000);
}

static void testAltNotificationRaces() {
  schedulerFixture(0x125000);
  storeBytes(0x126000, 4, NO_PROCESS);
  alt_(0);
  cpuState.areg = 1;
  cpuState.breg = 0x126000;
  enbc_(0);
  omrState.Enables |= INTERNALCH_TRAPENB;
  storeBytes(trapRecordAddress(LOW_PRIORITY, 3, false) + 8, 4, 0);
  storeBytes(0x127000 - 4, 4, 0x4000);
  enqueueProcess(0x127000, HIGH_PRIORITY);
  pollTimers();
  cpuState.areg = 0x77;
  cpuState.breg = 0x126000;
  expect("enabling ALT notification needs no scheduling trap", outbyte_(0) != 0, 0);
  expect("enabling owner restored from shadow", cpuState.wptr, 0x125000);
  expect("enabling owner is not enqueued twice", omrState.ProcQueueFPtr[LOW_PRIORITY], NO_PROCESS);
  altwt_(0);
  expect("notification before altwt avoids suspension", cpuState.running, 1);

  schedulerFixture(0x128000);
  storeBytes(0x129000, 4, NO_PROCESS);
  talt_(0);
  cpuState.areg = 1;
  cpuState.breg = 0x129000;
  enbc_(0);
  cpuState.areg = 1;
  cpuState.breg = 10;
  enbt_(0);
  taltwt_(0);
  cpuState.running = true;
  cpuState.wptr = 0x12a000;
  cpuState.areg = 0x88;
  cpuState.breg = 0x129000;
  outbyte_(0);
  expect("channel notification resumes timed ALT once", cpuState.wptr, 0x128000);
  advanceTimers(11U * 3888U);
  expect("expiry after channel wake cannot enqueue again", omrState.ProcQueueFPtr[LOW_PRIORITY], NO_PROCESS);
  uint32_t value = 0;
  readBytes(0x128000 - 20, 4, &value);
  expect("expired ALT records observation time", value, 11);
  cpuState.areg = 0x20;
  cpuState.breg = 1;
  cpuState.creg = 10;
  dist_(0);
  expect("ready timer can win by disabling order", cpuState.areg, 1);
  cpuState.areg = 0x40;
  cpuState.breg = 1;
  cpuState.creg = 0x129000;
  disc_(0);
  expect("later ready channel cannot replace selection", cpuState.areg, 0);
  readBytes(0x129000, 4, &value);
  expect("nonselected sender remains waiting", value, 0x12a001);
  schedulerFixture(0x20000);
}

static void testAltSnapshots() {
  char directory[] = "/tmp/st20-alt-snapshot-XXXXXX";
  if (!mkdtemp(directory)) {
    expect("ALT snapshot directory", 0, 1);
    return;
  }
  schedulerFixture(0x130000);
  storeBytes(0x131000, 4, NO_PROCESS);
  alt_(0);
  cpuState.areg = 1;
  cpuState.breg = 0x131000;
  enbc_(0);
  altwt_(0);
  expect("ALT waiting snapshot save", saveCPUState(directory) != 0, 0);
  resetScheduler();
  expect("ALT waiting snapshot load", loadCPUState(directory) != 0, 0);
  expect("ALT waiting snapshot restores block", cpuState.running, 0);
  cpuState.running = true;
  cpuState.priority = HIGH_PRIORITY;
  cpuState.wptr = 0x132000;
  cpuState.areg = 0x66;
  cpuState.breg = 0x131000;
  expect("restored ALT can receive notification", outbyte_(0) != 0, 0);
  expect("restored ALT owner resumes", cpuState.wptr, 0x130000);

  schedulerFixture(0x133000);
  talt_(0);
  cpuState.areg = 1;
  cpuState.breg = 5;
  enbt_(0);
  taltwt_(0);
  saveCPUState(directory);
  resetScheduler();
  expect("timed ALT snapshot load", loadCPUState(directory) != 0, 0);
  expect("restored timed ALT alarm", omrState.TnextReg[LOW_PRIORITY], 6);
  expect("restored timed ALT can wake and fetch", decodeNextInstr() != 0, 0);
  expect("restored timed ALT clock", omrState.ClockRegLP, 6);
  std::string filename = std::string(directory) + "/cpu.bin";
  std::remove(filename.c_str());
  rmdir(directory);
  schedulerFixture(0x20000);
}

static void testAltBoundaryCases() {
  schedulerFixture(0x140000);
  cpuState.areg = 3;
  tin_(0);
  cpuState.running = true;
  cpuState.wptr = 0x141000;
  storeBytes(0x142000, 4, NO_PROCESS);
  talt_(0);
  cpuState.areg = 1;
  cpuState.breg = 0x142000;
  enbc_(0);
  cpuState.areg = 1;
  cpuState.breg = 10;
  enbt_(0);
  taltwt_(0);
  cpuState.running = true;
  cpuState.wptr = 0x143000;
  cpuState.areg = 0x99;
  cpuState.breg = 0x142000;
  outbyte_(0);
  cpuState.areg = 0x20;
  cpuState.breg = 0;
  cpuState.creg = 10;
  dist_(0);
  expect("dist removes middle timer without selecting false guard", cpuState.areg, 0);
  expect("dist preserves unrelated timer head", omrState.TptrReg[LOW_PRIORITY], 0x140000);
  expect("dist preserves unrelated earliest alarm", omrState.TnextReg[LOW_PRIORITY], 4);
  uint32_t value = 0;
  readBytes(0x140000 - 16, 4, &value);
  expect("dist relinks unrelated timer", value, NO_PROCESS);

  schedulerFixture(0x144000);
  alt_(0);
  cpuState.areg = 0;
  enbs_(0);
  altwt_(0);
  expect("ALT with no ready guards remains blocked", cpuState.running, 0);
  expect("no enabled ALT events returns idle", decodeNextInstr() == CPU_IDLE, 1);

  schedulerFixture(0x145000);
  installTrap(LOW_PRIORITY, 3, 0x146000, 0x7000);
  omrState.Enables |= TIMER_TRAPENB;
  talt_(0);
  cpuState.areg = 1;
  cpuState.breg = 0;
  enbt_(0);
  taltwt_(0);
  expect("timed ALT expiry reaches timer kernel", advanceTimers(3888) != 0, 0);
  expect("timed ALT timer handler selected", cpuState.wptr, 0x146000);
  expect("timed ALT timer descriptor", cpuState.breg, 0x145001);
  cpuState.areg = cpuState.breg;
  runp_(0);
  tret_(0);
  pollTimers();
  expect("timer kernel can resume ALT owner", cpuState.wptr, 0x145000);
  cpuState.areg = 0x10;
  cpuState.breg = 1;
  cpuState.creg = 0;
  dist_(0);
  expect("trap-resumed ALT timer can select", cpuState.areg, 1);
  cpuState.iptr = 0x7100;
  storeBytes(0x7100, 2, 0xf524);
  decodeNextInstr();
  int watchFlag = 0;
  expect("encoded altend executes", execInstr(&watchFlag) != 0, 0);
  expect("altend offset uses following byte", cpuState.iptr, 0x7112);
  schedulerFixture(0x20000);
}

static void testAsyncTrace() {
  for (uint32_t opcode : {0x41U, 0x01U, 0xa1U, 0x91U, 0xf0U}) {
    for (int words : {0, 1, 5}) {
      schedulerFixture(0x20000);
      cpuState.nWptr = words;
      for (int index = 0; index < words; ++index) {
        storeBytes(cpuState.wptr + static_cast<uint32_t>(index) * BytesPerWord,
                   BytesPerWord, 0xabc00000U + static_cast<uint32_t>(index));
      }
      cpuState.iptr = 0x7100;
      storeBytes(cpuState.iptr, 1, opcode);
      expect("trace instruction decodes", decodeNextInstr() != 0, 0);
      CPUTrace trace;
      expect("trace workspace capture", captureCPUTrace(trace) != 0, 0);
      std::string expected;
      compat::outputBuffer = &expected;
      printNextInstr();
      printCPUState();
      cpuState.areg ^= WordMask;
      cpuState.iptr = 0x7200;
      storeBytes(cpuState.iptr, 1, 0x42);
      decodeNextInstr();
      storeBytes(cpuState.wptr, BytesPerWord, 0);
      std::string actual;
      std::thread formatter([&] {
        expect("worker buffer is independent", compat::outputBuffer == nullptr, 1);
        compat::outputBuffer = &actual;
        printNextInstr(trace);
        printCPUState(trace);
        compat::outputBuffer = nullptr;
      });
      formatter.join();
      expect("producer buffer is unchanged", compat::outputBuffer == &expected, 1);
      compat::outputBuffer = nullptr;
      expect("deferred trace preserves original text and memory", actual == expected, 1);
    }
  }
}

static int runSimulatorTrace(const char *filename, const char *countText, bool frozenClocks) {
  char *countEnd = nullptr;
  unsigned long instructionCount = std::strtoul(countText, &countEnd, 10);
  if (*countText == '\0' || *countEnd != '\0' || instructionCount == 0 || instructionCount > 1000000)
    return 2;
  std::ifstream input(filename);
  if (!input) return 2;
  if (memoryInit() || initCPUState()) return 2;
  std::string line;
  uint32_t entry = 0;
  bool first = true;
  while (std::getline(input, line)) {
    std::istringstream words(line);
    uint64_t address = 0;
    if (!(words >> std::hex >> address) || address > UINT32_MAX) return 2;
    if (first) entry = static_cast<uint32_t>(address);
    first = false;
    uint32_t value = 0;
    bool hasBytes = false;
    while (words >> std::hex >> value) {
      if (value > UINT8_MAX || address > UINT32_MAX || storeBytes(static_cast<uint32_t>(address), 1, value))
        return 2;
      ++address;
      hasBytes = true;
    }
    if (!words.eof() || !hasBytes) return 2;
  }
  if (first) return 2;
  setPortableInstructionAbi(false);
  cpuState = {};
  cpuState.areg = cpuState.breg = cpuState.creg = undefinedWord;
  cpuState.iptr = entry;
  cpuState.wptr = 0x80000100U;
  cpuState.priority = HIGH_PRIORITY;
  omrState = {};
  omrState.Enables = 0xffff0000U;
  std::string diagnostics;
  compat::outputBuffer = &diagnostics;
  int result = 0;
  for (unsigned long index = 0; index < instructionCount; ++index) {
    result = decodeNextInstr();
    if (result) break;
    std::printf("state %08" PRIx32 " %08" PRIx32 " %08" PRIx32 " %08" PRIx32
                " %08" PRIx32 " %u\n", cpuState.iptr, cpuState.wptr | cpuState.priority,
                cpuState.areg, cpuState.breg, cpuState.creg,
                static_cast<unsigned int>(omrState.HP_ErrorFlag));
    int breakFlag = 0;
    if (frozenClocks) {
      if (instrEntry[instrCode].cpucycles >= CPU_CLOCK / HP_TIMER_HZ) {
        result = INVALID_CPU_FILE;
        break;
      }
      omrState.ClockPhaseHP = 0;
      omrState.ClockPhaseLP = 0;
    }
    result = execInstr(&breakFlag);
    if (result) break;
  }
  compat::outputBuffer = nullptr;
  if (result) std::fprintf(stderr, "simulator trace failed: %d\n%s", result, diagnostics.c_str());
  return result ? 1 : 0;
}

int main(int argc, char **argv) {
  if (argc == 4 && std::string(argv[1]) == "--sim-trace")
    return runSimulatorTrace(argv[2], argv[3], false);
  if (argc == 5 && std::string(argv[1]) == "--sim-trace" && std::string(argv[4]) == "--frozen-clocks")
    return runSimulatorTrace(argv[2], argv[3], true);
  if (argc == 7 && std::string(argv[1]) == "--sim-trace" && std::string(argv[4]) == "--frozen-clocks" &&
      std::string(argv[5]) == "--product-id") {
    char *valueEnd = nullptr;
    unsigned long value = std::strtoul(argv[6], &valueEnd, 16);
    if (*argv[6] == '\0' || *valueEnd != '\0' || value > UINT32_MAX) return 2;
    st20ProductId = static_cast<uint32_t>(value);
    return runSimulatorTrace(argv[2], argv[3], true);
  }
  if (argc != 1) return 2;
  testWordStack();
  testCarryArithmetic();
  testRegisterOperations();
  testCheckInstructions();
  testGlobalInterrupts();
  testProductIdDispatch();
  testMultiplyAndShifts();
  testBitInstructions();
  testNormalization();
  testIndependentDispatch();
  testCrcInstructions();
  testSaturatingInstructions();
  testSignedLongProducts();
  testFractionMultiply();
  testUnpackSingle();
  testNativeFloatHelpers();
  testInfinityAndHaltMode();
  testLoopEnd();
  testTimerClocks();
  testRuntimeDefaults();
  testDeviceAccess();
  testDeviceMemory();
  testDeviceMove();
  testMemoryReaders();
  testLegacySnapshot();
  testRealWorkspace();
  testReadyQueueControls();
  testParallelEnd();
  testInterruptAndTimesliceControls();
  testTimerScheduler();
  testTimerOrderingAndPreemption();
  testSchedulerSnapshot();
  testSemaphores();
  testInternalChannels();
  testMove2d();
  testBlockedChannelSnapshot();
  testTrapRecordsAndMasks();
  testFractionAndParallelTraps();
  testTimesliceTrapsAndSnapshots();
  testPeriodicTimeslicing();
  testResumableMoves();
  testPortableRestartAndShadow();
  testPortableChannelCopy();
  testPortableInterruptController();
  testNativeExternalChannels();
  testPortableExternalChannels();
  testPortableMove2d();
  testPortableExternalSnapshots();
  testInterruptReturns();
  testShadowRegisters();
  testReadyQueueNotifications();
  testTrapEntryReturn();
  testTrapEnableEntryRules();
  testCapturedInvalidReturn();
  testTrapInstructionHooks();
  testSchedulerTrapDelivery();
  testTrapSnapshot();
  testGeneratedAndNestedTraps();
  testSignalAndChannelDelivery();
  testVersion2AndHaltSnapshot();
  testUntimedAlt();
  testTimedAlt();
  testNativeTimedExternalAlt();
  testAltNotificationRaces();
  testAltSnapshots();
  testAltBoundaryCases();
  testResetAnalysis();
  testPortableFloatHelpers();
  testAsyncTrace();
  std::printf("CPU instruction tests: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}