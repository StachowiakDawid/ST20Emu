#ifndef CLI_STATE_H
#define CLI_STATE_H

#pragma once

#include <string>

constexpr int COMMAND_ERROR_START{-3000};
constexpr int COMMAND_ERROR_END{-3999};

constexpr int GET_COMMAND_ERROR{-3000};
constexpr int INPUT_ERROR{-3001};
constexpr int BAD_PARAMETER{-3002};
constexpr int NO_COMMAND_FILE{-3010};
constexpr int UNKNOWN_COMMAND{-3011};
constexpr int NO_WATCHES_SET{-3012};

constexpr int COMMAND_LEN{256};

// public API
void commandsInit();
bool quitRequested();
void setQuit(bool flag);
bool needCmd();
void setNeedCmd(bool flag);
bool needPrompt();
int setNeedPrompt(bool flag);
bool showRegs();

int getCommand();
int execCommand();
const char *commandError(int error);

// internal
const std::string &getCmdName();
const std::string &getCmdParm1();
const std::string &getCmdParm2();
void toggleShowRegs();

#endif
