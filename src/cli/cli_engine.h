#ifndef CLI_ENGINE_H
#define CLI_ENGINE_H

#pragma once

#include "cli_types.h"

#include <span>
#include <string_view>
#include <string>
#include <vector>

namespace cli {

class CliEngine {
public:
  CliEngine() = default;

  // main interaction loop function
  CliError prompt_and_execute();

  // state accessors
  bool is_quit_requested() const {
    return quit_requested;
  }
  void request_quit() {
    quit_requested = true;
  }

  bool needs_cmd() const {
    return need_cmd;
  }
  void set_need_cmd(bool flag) {
    need_cmd = flag;
  }

  bool needs_prompt() const {
    return need_prompt;
  }
  CliError set_need_prompt(bool flag);

  bool is_verbose_regs() const {
    return show_verbose_regs;
  }
  void toggle_verbose_regs() {
    show_verbose_regs = !show_verbose_regs;
  }

private:
  CliError execute_command(std::string_view cmd_name, std::span<const std::string_view> args);

  bool quit_requested{false};
  bool need_cmd{true};
  bool need_prompt{true};
  bool show_verbose_regs{false};
};

} // namespace cli

#endif
