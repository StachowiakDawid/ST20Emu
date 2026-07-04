#include "commands.h"
#include "compat.h"
#include "defines.h"
#include "st20.h"
#include "memory.h"
#include "omr.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <ranges>
#include <sstream>
#include <string_view>
#include <string>
#include <unordered_map>

extern "C" int SearchForReg(FILE *, unsigned long);

namespace {

struct CMDSTATE {
  bool quit{false};
  bool needCmd{true};
  bool needPrompt{true};
  bool showVerboseRegs{false};
  std::string command;
  std::string parm1;
  std::string parm2;
};

CMDSTATE cmdState;

// -----------------------------------------------------------------------------
// Command implementations
// -----------------------------------------------------------------------------

int u_doError() {
  return INPUT_ERROR;
}

int u_go() {
  return setNeedPrompt(!needPrompt());
}

int u_load() {
  if (cmdState.parm1.empty())
    return BAD_PARAMETER;

  int result = loadMemory(cmdState.parm1.c_str(), stdout);
  if (!result) {
    result = loadCPUState(cmdState.parm1.c_str(), stdout);
  }

  printCPUState(stdout);
  return result;
}

int u_loadData() {
  uint32_t startAddr = 0;
  long dataLength = 0;
  int result = 0;

  if (!cmdState.parm2.empty() && parseHex(cmdState.parm1, startAddr)) {
    result = bulkLoadBytes(startAddr, cmdState.parm2.c_str(), nullptr, &dataLength);
  } else {
    result = bulkLoadBytes(0, cmdState.parm1.c_str(), nullptr, &dataLength);
    compat::println("Read {} bytes from {}", dataLength, cmdState.parm1);
  }
  return result;
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

int u_save() {
  if (cmdState.parm1.empty())
    return BAD_PARAMETER;

  int result = saveMemory(cmdState.parm1.c_str(), stdout);
  if (!result) {
    result = saveCPUState(cmdState.parm1.c_str(), stdout);
  }
  return result;
}

int u_setAreg() {
  long value;
  return parseHex(cmdState.parm1, value) ? setAreg(value) : BAD_PARAMETER;
}

int u_setBreg() {
  long value;
  return parseHex(cmdState.parm1, value) ? setBreg(value) : BAD_PARAMETER;
}

int u_setCreg() {
  long value;
  return parseHex(cmdState.parm1, value) ? setCreg(value) : BAD_PARAMETER;
}

int u_setIptr() {
  long value;
  return parseHex(cmdState.parm1, value) ? setIptr(value) : BAD_PARAMETER;
}

int u_stop() {
  return setWatch(cmdState.parm1.c_str(), cmdState.parm2.c_str());
}

int u_storeWptr() {
  long index, value;
  if (!parseHex(cmdState.parm1, index) || !parseHex(cmdState.parm2, value)) {
    return BAD_PARAMETER;
  }
  return storeWptrWord(index, value);
}

int u_view() {
  long address;
  unsigned long value = 0;

  if (!parseHex(cmdState.parm1, address))
    return BAD_PARAMETER;

  int result = readBytes(address, 4, &value);
  compat::println("Value at 0x{:08x} is 0x{:08x}", address, value);
  return result;
}

int u_view_a() {
  long address, value;

  if (!parseHex(cmdState.parm1, address) || !parseHex(cmdState.parm2, value)) {
    return BAD_PARAMETER;
  }

  int result = storeBytes(address, 4, value);
  compat::println("Value at 0x{:08x} is 0x{:08x}", address, value);
  return result;
}

int u_view_aa() {
  long address, range;

  if (!parseHex(cmdState.parm1, address) || !parseHex(cmdState.parm2, range)) {
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

  if (!parseHex(cmdState.parm1, n))
    return BAD_PARAMETER;

  int result = addrWptrWord(n, &value);
  compat::println("Wptr_n is at 0x{:08x}", value);
  return result;
}

int u_query_db() {
  unsigned long n = 0;
  if (!parseHex(cmdState.parm1, n))
    return BAD_PARAMETER;

  SearchForReg(stdout, n);
  return 0;
}

int u_showregs() {
  cmdState.showVerboseRegs = !cmdState.showVerboseRegs;
  compat::println("Verbose Register Access {}", cmdState.showVerboseRegs ? "ON" : "OFF");
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

// -----------------------------------------------------------------------------
// Command dictionary
// -----------------------------------------------------------------------------

using CmdFunc = int (*)();

const std::unordered_map<std::string_view, CmdFunc> uCommands = {
    {"", u_next},       {"a", u_setAreg},       {"b", u_setBreg},    {"c", u_setCreg},
    {"db", u_query_db}, {"doerror", u_doError}, {"g", u_go},         {"i", u_setIptr},
    {"l", u_loadData},  {"load", u_load},       {"omr", u_omr},      {"q", u_quit},
    {"s", u_stop},      {"save", u_save},       {"v", u_view},       {"va", u_view_a},
    {"vaa", u_view_aa}, {"ver", u_showenbreg},  {"vra", u_showregs}, {"vw", u_view_w},
    {"w", u_storeWptr}};

} // end anonymous namespace

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

void commandsInit() {
  initCmdState();
}

void initCmdState() {
  cmdState = CMDSTATE{};
}

bool quitRequested() {
  return cmdState.quit;
}

void setQuit(bool flag) {
  cmdState.quit = flag;
}

bool needCmd() {
  return cmdState.needCmd;
}

void setNeedCmd(bool flag) {
  cmdState.needCmd = flag;
}

bool needPrompt() {
  return cmdState.needPrompt;
}

bool showRegs() {
  return cmdState.showVerboseRegs;
}

int setNeedPrompt(bool flag) {
  cmdState.needPrompt = flag;

  if (!cmdState.needPrompt && !anyWatch()) {
    cmdState.needPrompt = true;
    return NO_WATCHES_SET;
  }
  return 0;
}

int getCommand() {
  cmdState.command = "doerror";
  cmdState.parm1.clear();
  cmdState.parm2.clear();

  compat::print("> ");
  std::fflush(stdout);

  char commandLine[COMMAND_LEN];
  if (std::fgets(commandLine, COMMAND_LEN, stdin) == nullptr) {
    return GET_COMMAND_ERROR;
  }

  std::istringstream iss{commandLine};
  std::string tempCmd;

  if (iss >> tempCmd) {
    cmdState.command = tempCmd;
    iss >> cmdState.parm1 >> cmdState.parm2;
  } else {
    cmdState.command.clear();
  }

  std::ranges::transform(cmdState.command, cmdState.command.begin(),
                         [](unsigned char c) { return std::tolower(c); });

  return 0;
}

int execCommand() {
  auto it = uCommands.find(cmdState.command);
  if (it == uCommands.end()) {
    return UNKNOWN_COMMAND;
  }
  return it->second();
}

const char *commandError(int error) {
  switch (error) {
  case GET_COMMAND_ERROR:
    return "Can't read command line";
  case INPUT_ERROR:
    return "Invalid input command or error reading command";
  case BAD_PARAMETER:
    return "Command parameter was missing or invalid";
  case NO_COMMAND_FILE:
    return "Can't read input command file";
  case UNKNOWN_COMMAND:
    return "Unknown command";
  case NO_WATCHES_SET:
    return "Prompting must be enabled if no watch conditions are set";
  default:
    return "Unknown command error";
  }
}
