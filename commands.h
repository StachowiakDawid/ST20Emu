#pragma once

#include <cstdio>

#include "defines.h"

constexpr int COMMAND_ERROR_START{-3000};
constexpr int COMMAND_ERROR_END{-3999};

constexpr int GET_COMMAND_ERROR{-3000};
constexpr int INPUT_ERROR{-3001};
constexpr int BAD_PARAMETER{-3002};
constexpr int NO_COMMAND_FILE{-3010};
constexpr int UNKNOWN_COMMAND{-3011};
constexpr int NO_WATCHES_SET{-3012};

constexpr int COMMAND_LEN{256};

void commandsInit();
bool quitRequested();
void setQuit(bool);
bool needCmd();
void setNeedCmd(bool);
bool needPrompt();
bool showRegs();
int setNeedPrompt(bool);
void initCmdState();
int getCommand();
int execCommand();
const char *commandError(int);
