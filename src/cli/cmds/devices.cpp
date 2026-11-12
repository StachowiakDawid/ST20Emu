#include "../cli_commands.h"

#include "../../board/Board.h"
#include "../../board/DSR9500.h"
#include "../../board/Stb6300.h"
#include "../../common/compat.h"
#include "../../soc/sti5512/STi5512_Devices.h"
#include "../../soc/sti5512/STi5512_Display.h"
#include "../../soc/sti5512/STi5512_RegisterBlocks.h"
#include "../../soc/sti5518/STi5518_Devices.h"
#include "../../soc/sti5518/STi5518_Display.h"
#include "../../soc/sti5518/STi5518_RegisterBlocks.h"

#include <string>

namespace cli::cmds {

CliError cmd_ir(CliEngine & /*engine*/, std::span<const std::string_view> args) {
  if (args.empty()) return CliError::BadParameter;
  for (std::string_view key : args) {
    std::string error;
    switch (activeBoard()) {
      case Board::Dsr9500:
      case Board::Dsr7000: error = dsr9500SendIr(key); break;
      case Board::Stb6300: error = stb6300SendIr(key); break;
      case Board::None: error = "remote control needs --board dsr9500, dsr7000 or 6300"; break;
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
    case Board::Dsr9500:
    case Board::Dsr7000: error = dsr9500PressFrontPanel(args[0]); break;
    case Board::Stb6300: error = stb6300SendKey(args[0]); break;
    case Board::None: error = "front panel needs --board dsr9500, dsr7000 or 6300"; break;
  }
  if (!error.empty()) {
    compat::println("{}", error);
    return CliError::BadParameter;
  }
  return CliError::Success;
}

/* asc PORT TEXT...: queue TEXT (words joined by spaces) and a newline on the receiver of
 * ASC PORT (0-3), e.g. typing into a Linux console on ASC3 */
CliError cmd_asc(CliEngine & /*engine*/, std::span<const std::string_view> args) {
  if (args.size() < 2 || args[0].size() != 1 || args[0][0] < '0' || args[0][0] > '3')
    return CliError::BadParameter;
  int port = args[0][0] - '0';
  std::vector<uint8_t> bytes;
  for (std::size_t i = 1; i < args.size(); ++i) {
    if (i > 1) bytes.push_back(' ');
    bytes.insert(bytes.end(), args[i].begin(), args[i].end());
  }
  bytes.push_back('\n');
  Board board = activeBoard();
  Sti55xxPeripherals &peripherals =
      board == Board::Dsr9500 || board == Board::Dsr7000 ? sti5512Peripherals() : sti5518Peripherals();
  peripherals.feedAscReceive(port, bytes);
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
