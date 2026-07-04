#include "cli_state.h"
#include "../utils/compat.h"
#include "../st20.h"
#include "../defines.h"
// #include "../core/st20.h"
// #include "../core/defines.h"

#include <cctype>
#include <cstdio>
#include <sstream>
#include <algorithm>
#include <ranges>

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
} // namespace

const std::string &getCmdName() {
  return cmdState.command;
}

const std::string &getCmdParm1() {
  return cmdState.parm1;
}
const std::string &getCmdParm2() {
  return cmdState.parm2;
}
void toggleShowRegs() {
  cmdState.showVerboseRegs = !cmdState.showVerboseRegs;
}

// public API
void commandsInit() {
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
