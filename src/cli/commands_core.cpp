#include "cli_state.h"
#include "cli_commands.h"
#include <string_view>
#include <unordered_map>

using CmdFunc = int (*)();

static const std::unordered_map<std::string_view, CmdFunc> uCommands = {
    {"", u_next},       {"a", u_setAreg},       {"b", u_setBreg},    {"c", u_setCreg},
    {"db", u_query_db}, {"doerror", u_doError}, {"g", u_go},         {"i", u_setIptr},
    {"l", u_loadData},  {"load", u_load},       {"omr", u_omr},      {"q", u_quit},
    {"s", u_stop},      {"save", u_save},       {"v", u_view},       {"va", u_view_a},
    {"vaa", u_view_aa}, {"ver", u_showenbreg},  {"vra", u_showregs}, {"vw", u_view_w},
    {"w", u_storeWptr}};

int execCommand() {
  auto it = uCommands.find(getCmdName());
  if (it == uCommands.end()) {
    return UNKNOWN_COMMAND;
  }
  return it->second();
}
