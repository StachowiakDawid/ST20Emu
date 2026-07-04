#include "../cli_commands.h"
#include "../cli_state.h"
#include "../../utils/compat.h"
// #include "../../core/st20.h"
// #include "../../core/omr.h"
// #include "../../core/defines.h"
#include "../../st20.h"
#include "../../omr.h"
#include "../../defines.h"

extern "C" int SearchForReg(FILE *, unsigned long);

int u_setAreg() {
  long value;
  return parseHex(getCmdParm1(), value) ? setAreg(value) : BAD_PARAMETER;
}

int u_setBreg() {
  long value;
  return parseHex(getCmdParm1(), value) ? setBreg(value) : BAD_PARAMETER;
}

int u_setCreg() {
  long value;
  return parseHex(getCmdParm1(), value) ? setCreg(value) : BAD_PARAMETER;
}

int u_setIptr() {
  long value;
  return parseHex(getCmdParm1(), value) ? setIptr(value) : BAD_PARAMETER;
}

int u_storeWptr() {
  long index, value;
  if (!parseHex(getCmdParm1(), index) || !parseHex(getCmdParm2(), value)) {
    return BAD_PARAMETER;
  }
  return storeWptrWord(index, value);
}

int u_showregs() {
  toggleShowRegs();
  compat::println("Verbose Register Access {}", showRegs() ? "ON" : "OFF");
  return 0;
}

int u_query_db() {
  unsigned long n = 0;
  if (!parseHex(getCmdParm1(), n))
    return BAD_PARAMETER;

  SearchForReg(stdout, n);
  return 0;
}

int u_showenbreg() {
  printEnablesRegState(stdout);
  return 0;
}

int u_omr() {
  printOMRState(stdout);
  return 0;
}
