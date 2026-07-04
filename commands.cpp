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

struct CMDSTATE {
  bool quit{false};
  bool needCmd{true};
  bool needPrompt{true};
  bool showVerboseRegs{false};
  std::string command;
  std::string parm1;
  std::string parm2;
};

static CMDSTATE cmdState;

using CmdFunc = int (*)();

int u_next();
int u_setAreg();
int u_setBreg();
int u_setCreg();
int u_doError();
int u_setIptr();
int u_go();
int u_load();
int u_loadData();
int u_quit();
int u_save();
int u_stop();
int u_storeWptr();
int u_view();
int u_view_a();
int u_view_w();
int u_view_aa();
int u_query_db();
int u_showregs();
int u_showenbreg();
int u_omr();

static const std::unordered_map<std::string_view, CmdFunc> uCommands = {
    {"", u_next},       {"a", u_setAreg},       {"b", u_setBreg},    {"c", u_setCreg},
    {"db", u_query_db}, {"doerror", u_doError}, {"g", u_go},         {"i", u_setIptr},
    {"l", u_loadData},  {"load", u_load},       {"omr", u_omr},      {"q", u_quit},
    {"s", u_stop},      {"save", u_save},       {"v", u_view},       {"va", u_view_a},
    {"vaa", u_view_aa}, {"ver", u_showenbreg},  {"vra", u_showregs}, {"vw", u_view_w},
    {"w", u_storeWptr}};

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

  if (!cmdState.needPrompt) {
    if (!anyWatch()) {
      cmdState.needPrompt = true;
      return NO_WATCHES_SET;
    }
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

int u_doError() {
  return INPUT_ERROR;
}

/* GO */
int u_go() {
  return setNeedPrompt(!needPrompt());
}

/* LOAD */
int u_load() {
  if (cmdState.parm1.empty())
    return BAD_PARAMETER;

  int result = loadMemory(cmdState.parm1.c_str(), stdout);
  if (!result)
    result = loadCPUState(cmdState.parm1.c_str(), stdout);

  printCPUState(stdout);
  return result;
}

/* LOAD DATA */
int u_loadData() {
  uint32_t startAddr = 0; // forced to 32-bit to mirror the original startAddr & 0xFFFFFFFF
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

/* NEXT */
int u_next() {
  setNeedCmd(false);
  return 0;
}

/* QUIT */
int u_quit() {
  setQuit(true);
  setNeedCmd(false);
  return 0;
}

/* SAVE */
int u_save() {
  if (cmdState.parm1.empty()) {
    return BAD_PARAMETER;
  }

  int result = saveMemory(cmdState.parm1.c_str(), stdout);
  if (!result)
    result = saveCPUState(cmdState.parm1.c_str(), stdout);

  return result;
}

/* setAreg */
int u_setAreg() {
  long value;
  if (parseHex(cmdState.parm1, value))
    return setAreg(value);

  return BAD_PARAMETER;
}

/* setBreg */
int u_setBreg() {
  long value;
  if (parseHex(cmdState.parm1, value))
    return setBreg(value);

  return BAD_PARAMETER;
}

/* setCreg */
int u_setCreg() {
  long value;
  if (parseHex(cmdState.parm1, value))
    return setCreg(value);

  return BAD_PARAMETER;
}

/* setIptr */
int u_setIptr() {
  long value;
  if (parseHex(cmdState.parm1, value))
    return setIptr(value);

  return BAD_PARAMETER;
}

/* stop */
int u_stop() {
  return setWatch(cmdState.parm1.c_str(), cmdState.parm2.c_str());
}

/* storeWptr */
int u_storeWptr() {
  long index, value;

  if (!parseHex(cmdState.parm1, index) || !parseHex(cmdState.parm2, value))
    return BAD_PARAMETER;

  return storeWptrWord(index, value);
}

/* view */
int u_view() {
  long address;
  unsigned long value = 0;

  if (!parseHex(cmdState.parm1, address))
    return BAD_PARAMETER;

  int result = readBytes(address, 4, &value);
  compat::println("Value at 0x{:08x} is 0x{:08x}", address, value);

  return result;
}

/* view_a */
int u_view_a() {
  long address, value;

  if (!parseHex(cmdState.parm1, address) || !parseHex(cmdState.parm2, value))
    return BAD_PARAMETER;

  int result = storeBytes(address, 4, value);
  compat::println("Value at 0x{:08x} is 0x{:08x}", address, value);

  return result;
}

/* view_aa */
int u_view_aa() {
  long address, range;

  if (!parseHex(cmdState.parm1, address) || !parseHex(cmdState.parm2, range))
    return BAD_PARAMETER;

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

/* view_w */
int u_view_w() {
  long n, value;

  if (!parseHex(cmdState.parm1, n))
    return BAD_PARAMETER;

  int result = addrWptrWord(n, &value);
  compat::println("Wptr_n is at 0x{:08x}", value);

  return result;
}

// QUERY REGISTER NAME BY VALUE
int u_query_db() {
  unsigned long n = 0;

  if (!parseHex(cmdState.parm1, n))
    return BAD_PARAMETER;

  SearchForReg(stdout, n);
  return 0;
}

// SET THE "SHOW REGISTER ACCESS" FLAG
int u_showregs() {
  // Flag flip-flopping & show status
  cmdState.showVerboseRegs = !cmdState.showVerboseRegs;
  compat::println("Verbose Register Access {}", cmdState.showVerboseRegs ? "ON" : "OFF");

  return 0;
}

// SHOW THE ENABLES REGISTER CONTENTS, BIT by BIT
int u_showenbreg() {
  printEnablesRegState(stdout);
  return 0;
}

// SHOW THE 'OTHER MACHINE REGISTER' CONTENTS
int u_omr() {
  printOMRState(stdout);
  return 0;
}

const char *commandError(int error) {
  switch (error) {

  case GET_COMMAND_ERROR:
    return ("Can't read command line");
    break;

  case INPUT_ERROR:
    return ("Invalid input command or error reading command");
    break;

  case BAD_PARAMETER:
    return ("Command parameter was missing or invalid");
    break;

  case NO_COMMAND_FILE:
    return ("Can't read input command file");
    break;

  case UNKNOWN_COMMAND:
    return ("Unknown command");
    break;

  case NO_WATCHES_SET:
    return ("Prompting must be enabled if no watch conditions are set");
    break;

  default:
    return ("Unknown command error");
    break;
  }

  return (NULL);
}
