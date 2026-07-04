#include "../cli_commands.h"
#include "../cli_state.h"
#include "../../defines.h"
// #include "../../core/defines.h"
#include "../../st20.h"

int u_doError() {
  return INPUT_ERROR;
}

int u_go() {
  return setNeedPrompt(!needPrompt());
}

int u_next() {
  setNeedCmd(false);
  return 0;
}

int u_quit() {
  setQuit(true);
  setNeedCmd(false);
  return 0;
}

int u_stop() {
  return setWatch(getCmdParm1().c_str(), getCmdParm2().c_str());
}
