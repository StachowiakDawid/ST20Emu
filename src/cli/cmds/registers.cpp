#include "../cli_commands.h"
#include "../../utils/compat.h"
#include "../../st20.h"
#include <string>

extern "C" int SearchForReg(FILE *, unsigned long);

namespace cli::cmds {

CliError cmd_set_areg(CliEngine & /*engine*/, std::span<const std::string_view> args) {
  long value;
  if (args.empty() || !parseHex(std::string(args[0]), value))
    return CliError::BadParameter;

  setAreg(value);
  return CliError::Success;
}

CliError cmd_set_breg(CliEngine & /*engine*/, std::span<const std::string_view> args) {
  long value;
  if (args.empty() || !parseHex(std::string(args[0]), value))
    return CliError::BadParameter;

  setBreg(value);
  return CliError::Success;
}

CliError cmd_set_creg(CliEngine & /*engine*/, std::span<const std::string_view> args) {
  long value;
  if (args.empty() || !parseHex(std::string(args[0]), value))
    return CliError::BadParameter;

  setCreg(value);
  return CliError::Success;
}

CliError cmd_set_iptr(CliEngine & /*engine*/, std::span<const std::string_view> args) {
  long value;
  if (args.empty() || !parseHex(std::string(args[0]), value))
    return CliError::BadParameter;

  setIptr(value);
  return CliError::Success;
}

CliError cmd_store_wptr(CliEngine & /*engine*/, std::span<const std::string_view> args) {
  long index, value;
  if (args.size() < 2 || !parseHex(std::string(args[0]), index) ||
      !parseHex(std::string(args[1]), value)) {
    return CliError::BadParameter;
  }

  storeWptrWord(index, value);
  return CliError::Success;
}

CliError cmd_show_regs(CliEngine &engine, std::span<const std::string_view> /*args*/) {
  engine.toggle_verbose_regs();
  compat::println("Verbose Register Access {}", engine.is_verbose_regs() ? "ON" : "OFF");
  return CliError::Success;
}

CliError cmd_query_db(CliEngine & /*engine*/, std::span<const std::string_view> args) {
  unsigned long n = 0;
  if (args.empty() || !parseHex(std::string(args[0]), n))
    return CliError::BadParameter;

  SearchForReg(stdout, n);
  return CliError::Success;
}

CliError cmd_show_enbreg(CliEngine & /*engine*/, std::span<const std::string_view> /*args*/) {
  printEnablesRegState(stdout);
  return CliError::Success;
}

CliError cmd_omr(CliEngine & /*engine*/, std::span<const std::string_view> /*args*/) {
  printOMRState(stdout);
  return CliError::Success;
}

} // namespace cli::cmds
