#include "../cli_commands.h"
#include "../cli_state.h"
#include "../../utils/compat.h"
// #include "../../core/memory.h"
// #include "../../core/defines.h"
#include "../../st20.h"
#include "../../memory.h"
#include "../../defines.h"
#include <cstdint>

int u_load() {
  const auto &p1 = getCmdParm1();
  if (p1.empty())
    return BAD_PARAMETER;

  int result = loadMemory(p1.c_str(), stdout);
  if (!result) {
    result = loadCPUState(p1.c_str(), stdout);
  }

  printCPUState(stdout);
  return result;
}

int u_loadData() {
  uint32_t startAddr = 0;
  long dataLength = 0;
  int result = 0;

  if (!getCmdParm2().empty() && parseHex(getCmdParm1(), startAddr)) {
    result = bulkLoadBytes(startAddr, getCmdParm2().c_str(), nullptr, &dataLength);
  } else {
    result = bulkLoadBytes(0, getCmdParm1().c_str(), nullptr, &dataLength);
    compat::println("Read {} bytes from {}", dataLength, getCmdParm1());
  }
  return result;
}

int u_save() {
  if (getCmdParm1().empty())
    return BAD_PARAMETER;

  int result = saveMemory(getCmdParm1().c_str(), stdout);
  if (!result) {
    result = saveCPUState(getCmdParm1().c_str(), stdout);
  }
  return result;
}

int u_view() {
  long address;
  unsigned long value = 0;

  if (!parseHex(getCmdParm1(), address))
    return BAD_PARAMETER;

  int result = readBytes(address, 4, &value);
  compat::println("Value at 0x{:08x} is 0x{:08x}", address, value);
  return result;
}

int u_view_a() {
  long address, value;

  if (!parseHex(getCmdParm1(), address) || !parseHex(getCmdParm2(), value)) {
    return BAD_PARAMETER;
  }

  int result = storeBytes(address, 4, value);
  compat::println("Value at 0x{:08x} is 0x{:08x}", address, value);
  return result;
}

int u_view_aa() {
  long address, range;

  if (!parseHex(getCmdParm1(), address) || !parseHex(getCmdParm2(), range)) {
    return BAD_PARAMETER;
  }

  int result = 0;
  for (long i = address; i < (address + range); i += 4) {
    long value = 0;
    result = readInvBytes(i, 4, &value);

    if (!(i & 0x0f)) {
      compat::print("\n\r0x{:08x} : {:08x}", i, value);
    } else {
      compat::print(" 0x{:08x}", value);
    }
  }
  compat::print("\n\r");
  return result;
}

int u_view_w() {
  long n, value;

  if (!parseHex(getCmdParm1(), n))
    return BAD_PARAMETER;

  int result = addrWptrWord(n, &value);
  compat::println("Wptr_n is at 0x{:08x}", value);
  return result;
}
