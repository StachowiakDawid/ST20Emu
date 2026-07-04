#include "../cli_commands.h"
#include "../../core/cpu/st20.h"
#include <string>

namespace cli::cmds {

CliError cmd_do_error(CliEngine & /*engine*/, std::span<const std::string_view> /*args*/) {
  return CliError::InputError;
}

CliError cmd_go(CliEngine &engine, std::span<const std::string_view> /*args*/) {
  return engine.set_need_prompt(!engine.needs_prompt());
}

CliError cmd_next(CliEngine &engine, std::span<const std::string_view> /*args*/) {
  engine.set_need_cmd(false);
  return CliError::Success;
}

CliError cmd_quit(CliEngine &engine, std::span<const std::string_view> /*args*/) {
  engine.request_quit();
  engine.set_need_cmd(false);
  return CliError::Success;
}

CliError cmd_stop(CliEngine & /*engine*/, std::span<const std::string_view> args) {
  std::string p1 = args.size() > 0 ? std::string(args[0]) : "";
  std::string p2 = args.size() > 1 ? std::string(args[1]) : "";

  // TODO: assuming setWatch returns 0 on success in the legacy backend
  int result = setWatch(p1.c_str(), p2.c_str());
  return result == 0 ? CliError::Success : CliError::InputError;
}

} // namespace cli::cmds
