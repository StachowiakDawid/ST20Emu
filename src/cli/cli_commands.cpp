#include "cli_commands.h"

namespace cli {

const std::unordered_map<std::string_view, CmdFunc> cmd_dictionary = {
    {"", cmds::cmd_next},         {"a", cmds::cmd_set_areg},  {"b", cmds::cmd_set_breg},
    {"c", cmds::cmd_set_creg},    {"db", cmds::cmd_query_db}, {"doerror", cmds::cmd_do_error},
    {"g", cmds::cmd_go},          {"i", cmds::cmd_set_iptr},  {"l", cmds::cmd_load_data},
    {"load", cmds::cmd_load},     {"omr", cmds::cmd_omr},     {"q", cmds::cmd_quit},
    {"s", cmds::cmd_stop},        {"save", cmds::cmd_save},   {"v", cmds::cmd_view},
    {"va", cmds::cmd_view_a},     {"vaa", cmds::cmd_view_aa}, {"ver", cmds::cmd_show_enbreg},
    {"vra", cmds::cmd_show_regs}, {"vw", cmds::cmd_view_w},   {"w", cmds::cmd_store_wptr}};

} // namespace cli
