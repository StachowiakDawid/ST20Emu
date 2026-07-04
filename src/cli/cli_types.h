#ifndef CLI_TYPES_H
#define CLI_TYPES_H

#pragma once

namespace cli {

enum class CliError {
  Success,
  GetCommandError = -3000, // using old codes for compatibility
  InputError = -3001,
  BadParameter = -3002,
  NoCommandFile = -3010,
  UnknownCommand = -3011,
  NoWatchesSet = -3012,
  // TODO: add emulator-specific hardware errors here later if needed
};

const char *format_error(CliError error);

} // namespace cli

#endif
