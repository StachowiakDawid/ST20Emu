#ifndef CLI_COMMANDS_H
#define CLI_COMMANDS_H

#pragma once

#include "cli_engine.h"
#include <span>
#include <string_view>
#include <unordered_map>

namespace cli {

using CmdFunc = CliError (*)(CliEngine &engine, std::span<const std::string_view> args);

// defined in cli_commands.cpp
extern const std::unordered_map<std::string_view, CmdFunc> cmd_dictionary;

namespace cmds {
// control
CliError cmd_do_error(CliEngine &engine, std::span<const std::string_view> args);
CliError cmd_go(CliEngine &engine, std::span<const std::string_view> args);
CliError cmd_next(CliEngine &engine, std::span<const std::string_view> args);
CliError cmd_quit(CliEngine &engine, std::span<const std::string_view> args);
CliError cmd_stop(CliEngine &engine, std::span<const std::string_view> args);

// registers
CliError cmd_set_areg(CliEngine &engine, std::span<const std::string_view> args);
CliError cmd_set_breg(CliEngine &engine, std::span<const std::string_view> args);
CliError cmd_set_creg(CliEngine &engine, std::span<const std::string_view> args);
CliError cmd_set_iptr(CliEngine &engine, std::span<const std::string_view> args);
CliError cmd_store_wptr(CliEngine &engine, std::span<const std::string_view> args);
CliError cmd_show_regs(CliEngine &engine, std::span<const std::string_view> args);
CliError cmd_query_db(CliEngine &engine, std::span<const std::string_view> args);
CliError cmd_show_enbreg(CliEngine &engine, std::span<const std::string_view> args);
CliError cmd_omr(CliEngine &engine, std::span<const std::string_view> args);

// memory
CliError cmd_load(CliEngine &engine, std::span<const std::string_view> args);
CliError cmd_load_data(CliEngine &engine, std::span<const std::string_view> args);
CliError cmd_save(CliEngine &engine, std::span<const std::string_view> args);
CliError cmd_view(CliEngine &engine, std::span<const std::string_view> args);
CliError cmd_view_a(CliEngine &engine, std::span<const std::string_view> args);
CliError cmd_view_aa(CliEngine &engine, std::span<const std::string_view> args);
CliError cmd_view_w(CliEngine &engine, std::span<const std::string_view> args);
} // namespace cmds

} // namespace cli

#endif
