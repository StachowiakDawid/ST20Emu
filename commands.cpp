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

using CmdFunc = int (*)(FILE *, FILE *);

extern "C" {
int u_next(FILE *, FILE *);
int u_setAreg(FILE *, FILE *);
int u_setBreg(FILE *, FILE *);
int u_setCreg(FILE *, FILE *);
int u_doError(FILE *, FILE *);
int u_setIptr(FILE *, FILE *);
int u_go(FILE *, FILE *);
int u_load(FILE *, FILE *);
int u_loadData(FILE *, FILE *);
int u_quit(FILE *, FILE *);
int u_save(FILE *, FILE *);
int u_stop(FILE *, FILE *);
int u_storeWptr(FILE *, FILE *);
int u_view(FILE *, FILE *);
int u_view_a(FILE *, FILE *);
int u_view_w(FILE *, FILE *);
int u_view_aa(FILE *, FILE *);
int u_query_db(FILE *, FILE *);
int u_showregs(FILE *, FILE *);
int u_showenbreg(FILE *, FILE *);
int u_omr(FILE *, FILE *);
}

static const std::unordered_map<std::string_view, CmdFunc> uCommands = {
    {"", u_next},       {"a", u_setAreg},       {"b", u_setBreg},    {"c", u_setCreg},
    {"db", u_query_db}, {"doerror", u_doError}, {"g", u_go},         {"i", u_setIptr},
    {"l", u_loadData},  {"load", u_load},       {"omr", u_omr},      {"q", u_quit},
    {"s", u_stop},      {"save", u_save},       {"v", u_view},       {"va", u_view_a},
    {"vaa", u_view_aa}, {"ver", u_showenbreg},  {"vra", u_showregs}, {"vw", u_view_w},
    {"w", u_storeWptr}};

int commandsInit(PARMS *userParm, FILE *outFp) {
  initCmdState();
  return 0;
}

int initCmdState() {
  cmdState = CMDSTATE{};
  return 0;
}

bool quitRequested() {
  return cmdState.quit;
}

int setQuit(bool flag) {
  cmdState.quit = flag;
  return 0;
}

bool needCmd() {
  return cmdState.needCmd;
}

int setNeedCmd(bool flag) {
  cmdState.needCmd = flag;
  return 0;
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

int getCommand(FILE *inFp, FILE *outFp) {
  cmdState.command = "doerror";
  cmdState.parm1.clear();
  cmdState.parm2.clear();

  compat::print(outFp, "> ");
  std::fflush(outFp);

  char commandLine[COMMAND_LEN];
  if (std::fgets(commandLine, COMMAND_LEN, inFp) == nullptr) {
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

int execCommand(FILE *inFp, FILE *outFp) {
  auto it = uCommands.find(cmdState.command);
  if (it == uCommands.end()) {
    return UNKNOWN_COMMAND;
  }
  return it->second(inFp, outFp);
}

int u_doError(FILE *inFp, FILE *outFp) {
  return (INPUT_ERROR);
}

/* GO */
int u_go(FILE *inFp, FILE *outFp) {
  return setNeedPrompt(!needPrompt());
}

/* LOAD */
int u_load(FILE *inFp, FILE *outFp) {
  if (cmdState.parm1.empty())
    return BAD_PARAMETER;

  int result = loadMemory(cmdState.parm1.c_str(), outFp);
  if (!result)
    result = loadCPUState(cmdState.parm1.c_str(), outFp);

  printCPUState(outFp);
  return result;
}

/* LOAD DATA */
int u_loadData(FILE *inFp, FILE *outFp) {
  uint32_t startAddr = 0; // forced to 32-bit to mirror the original startAddr & 0xFFFFFFFF
  long dataLength = 0;
  int result = 0;

  if (!cmdState.parm2.empty() && parseHex(cmdState.parm1, startAddr)) {
    result = bulkLoadBytes(startAddr, cmdState.parm2.c_str(), nullptr, &dataLength);
  } else {
    result = bulkLoadBytes(0, cmdState.parm1.c_str(), nullptr, &dataLength);
    compat::println(outFp, "Read {} bytes from {}", dataLength, cmdState.parm1);
  }
  return result;
}

/* NEXT */
int u_next(FILE *inFp, FILE *outFp) {
  setNeedCmd(false);
  return 0;
}

/* QUIT */
int u_quit(FILE *inFp, FILE *outFp) {
  setQuit(true);
  setNeedCmd(false);
  return 0;
}

/* SAVE */
int u_save(FILE *inFp, FILE *outFp) {
  if (cmdState.parm1.empty()) {
    return BAD_PARAMETER;
  }

  int result = saveMemory(cmdState.parm1.c_str(), outFp);
  if (!result)
    result = saveCPUState(cmdState.parm1.c_str(), outFp);

  return result;
}

/* setAreg */
int u_setAreg(FILE *inFp, FILE *outFp) {
  long value;
  if (parseHex(cmdState.parm1, value))
    return setAreg(value);

  return BAD_PARAMETER;
}

/* setBreg */
int u_setBreg(FILE *inFp, FILE *outFp) {
  long value;
  if (parseHex(cmdState.parm1, value))
    return setBreg(value);

  return BAD_PARAMETER;
}

/* setCreg */
int u_setCreg(FILE *inFp, FILE *outFp) {
  long value;
  if (parseHex(cmdState.parm1, value))
    return setCreg(value);

  return BAD_PARAMETER;
}

/* setIptr */
int u_setIptr(FILE *inFp, FILE *outFp) {
  long value;
  if (parseHex(cmdState.parm1, value))
    return setIptr(value);

  return BAD_PARAMETER;
}

/* stop */
int u_stop(FILE *inFp, FILE *outFp) {
  return setWatch(cmdState.parm1.c_str(), cmdState.parm2.c_str());
}

/* storeWptr */
int u_storeWptr(FILE *inFp, FILE *outFp) {
  long index, value;

  if (!parseHex(cmdState.parm1, index) || !parseHex(cmdState.parm2, value))
    return BAD_PARAMETER;

  return storeWptrWord(index, value);
}

/* view */
int u_view(FILE *inFp, FILE *outFp) {
  long address;
  unsigned long value = 0;

  if (!parseHex(cmdState.parm1, address))
    return BAD_PARAMETER;

  int result = readBytes(address, 4, &value);
  compat::println(outFp, "Value at 0x{:08x} is 0x{:08x}", address, value);

  return result;
}

/* view_a */
int u_view_a(FILE *inFp, FILE *outFp) {
  long address, value;

  if (!parseHex(cmdState.parm1, address) || !parseHex(cmdState.parm2, value))
    return BAD_PARAMETER;

  int result = storeBytes(address, 4, value);
  compat::println(outFp, "Value at 0x{:08x} is 0x{:08x}", address, value);

  return result;
}

/* view_aa */
int u_view_aa(FILE *inFp, FILE *outFp) {
  long address, range;

  if (!parseHex(cmdState.parm1, address) || !parseHex(cmdState.parm2, range))
    return BAD_PARAMETER;

  int result = 0;
  for (long i = address; i < (address + range); i += 4) {
    long value = 0;
    result = readInvBytes(i, 4, &value);

    if (!(i & 0x0f)) {
      compat::print(outFp, "\n\r0x{:08x} : {:08x}", i, value);
    } else {
      compat::print(outFp, " 0x{:08x}", value);
    }
  }
  compat::print(outFp, "\n\r");
  return result;
}

/* view_w */
int u_view_w(FILE *inFp, FILE *outFp) {
  long n, value;

  if (!parseHex(cmdState.parm1, n))
    return BAD_PARAMETER;

  int result = addrWptrWord(n, &value);
  compat::println(outFp, "Wptr_n is at 0x{:08x}", value);

  return result;
}

// QUERY REGISTER NAME BY VALUE
int u_query_db(FILE *inFp, FILE *outFp) {
  unsigned long n = 0;

  if (!parseHex(cmdState.parm1, n))
    return BAD_PARAMETER;

  SearchForReg(outFp, n);
  return 0;
}

// SET THE "SHOW REGISTER ACCESS" FLAG
int u_showregs(FILE *inFp, FILE *outFp) {
  // Flag flip-flopping & show status
  cmdState.showVerboseRegs = !cmdState.showVerboseRegs;
  compat::println(outFp, "Verbose Register Access {}", cmdState.showVerboseRegs ? "ON" : "OFF");

  return 0;
}

// SHOW THE ENABLES REGISTER CONTENTS, BIT by BIT
int u_showenbreg(FILE *inFp, FILE *outFp) {
  printEnablesRegState(outFp);
  return 0;
}

// SHOW THE 'OTHER MACHINE REGISTER' CONTENTS
int u_omr(FILE *inFp, FILE *outFp) {
  printOMRState(outFp);
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
