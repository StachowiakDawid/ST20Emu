#include "../cli_commands.h"

#include "../../board/Board.h"
#include "../../board/DSR9500.h"
#include "../../board/Stb6300.h"
#include "../../common/compat.h"
#include "../../soc/sti5512/STi5512_Display.h"
#include "../../soc/sti5512/STi5512_RegisterBlocks.h"
#include "../../soc/sti5518/STi5518_Display.h"
#include "../../soc/sti5518/STi5518_RegisterBlocks.h"

#include <string>

namespace cli::cmds {

CliError cmd_ir(CliEngine & /*engine*/, std::span<const std::string_view> args) {
  if (args.empty()) return CliError::BadParameter;
  for (std::string_view key : args) {
    std::string error;
    switch (activeBoard()) {
      case Board::Dsr9500: error = dsr9500SendIr(key); break;
      case Board::Stb6300: error = stb6300SendIr(key); break;
      case Board::None: error = "remote control needs --board dsr9500 or 6300"; break;
    }
    if (!error.empty()) {
      compat::println("{}", error);
      return CliError::BadParameter;
    }
  }
  return CliError::Success;
}

CliError cmd_front_panel(CliEngine & /*engine*/, std::span<const std::string_view> args) {
  if (args.size() != 1) return CliError::BadParameter;
  std::string error;
  switch (activeBoard()) {
    case Board::Dsr9500: error = dsr9500PressFrontPanel(args[0]); break;
    case Board::Stb6300: error = stb6300SendKey(args[0]); break;
    case Board::None: error = "front panel needs --board dsr9500 or 6300"; break;
  }
  if (!error.empty()) {
    compat::println("{}", error);
    return CliError::BadParameter;
  }
  return CliError::Success;
}

CliError cmd_led(CliEngine & /*engine*/, std::span<const std::string_view> /*args*/) {
  if (stb6300Active()) compat::println("LED> {}", stb6300Display());
  else compat::println("LED> {} (segments {})", dsr9500Display(), dsr9500DisplaySegments());
  return CliError::Success;
}

CliError cmd_screen(CliEngine & /*engine*/, std::span<const std::string_view> args) {
  std::string path = args.empty() ? "screen.bmp" : std::string(args[0]);
  int regions = 0;
  if (stb6300Active()) {
    std::string error = writeSti5518Screen(path, &regions);
    if (!error.empty()) {
      compat::println("{}", error);
      return CliError::BadParameter;
    }
    compat::println("Wrote {} ({} OSD regions; OSD_CFG=0x{:02x} OSD_OTP=0x{:04x} OSD_OBP=0x{:04x})", path,
                    regions, sti5518VideoRegister(0x91), sti5518OsdPointer(0), sti5518OsdPointer(1));
    return CliError::Success;
  }
  std::string error = writeSti5512Screen(path, &regions);
  if (!error.empty()) {
    compat::println("{}", error);
    return CliError::BadParameter;
  }
  compat::println("Wrote {} ({} OSD regions; OSD_CFG=0x{:02x} OSD_TOP=0x{:04x} OSD_BOT=0x{:04x})", path,
                  regions, sti5512VideoRegister(0x91), sti5512OsdPointer(0), sti5512OsdPointer(1));
  return CliError::Success;
}

} // namespace cli::cmds
