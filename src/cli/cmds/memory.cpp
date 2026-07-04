#include "../cli_commands.h"
#include "../../common/compat.h"
#include "../../core/memory/memory.h"
#include "../../core/cpu/st20.h"
#include <cstdint>
#include <string>

namespace cli::cmds {
CliError cmd_load(CliEngine & /*engine*/, std::span<const std::string_view> args) {
  if (args.empty())
    return CliError::BadParameter;

  std::string file_path(args[0]);
  int result = loadMemory(file_path.c_str(), stdout);
  if (result == 0)
    result = loadCPUState(file_path.c_str(), stdout);

  printCPUState(stdout);
  return result == 0 ? CliError::Success : CliError::InputError;
}

CliError cmd_load_data(CliEngine & /*engine*/, std::span<const std::string_view> args) {
  if (args.empty())
    return CliError::BadParameter;

  uint32_t startAddr = 0;
  long dataLength = 0;
  int result = 0;

  if (args.size() >= 2 && parseHex(std::string(args[0]), startAddr)) {
    std::string file_path(args[1]);
    result = bulkLoadBytes(startAddr, file_path.c_str(), nullptr, &dataLength);
  } else {
    std::string file_path(args[0]);
    result = bulkLoadBytes(0, file_path.c_str(), nullptr, &dataLength);
    compat::println("Read {} bytes from {}", dataLength, file_path);
  }

  return result == 0 ? CliError::Success : CliError::InputError;
}

CliError cmd_save(CliEngine & /*engine*/, std::span<const std::string_view> args) {
  if (args.empty())
    return CliError::BadParameter;

  std::string file_path(args[0]);
  int result = saveMemory(file_path.c_str(), stdout);
  if (result == 0) {
    result = saveCPUState(file_path.c_str(), stdout);
  }

  return result == 0 ? CliError::Success : CliError::InputError;
}

CliError cmd_view(CliEngine & /*engine*/, std::span<const std::string_view> args) {
  long address;
  unsigned long value = 0;

  if (args.empty() || !parseHex(std::string(args[0]), address))
    return CliError::BadParameter;

  readBytes(address, 4, &value);
  compat::println("Value at 0x{:08x} is 0x{:08x}", address, value);
  return CliError::Success;
}

CliError cmd_view_a(CliEngine & /*engine*/, std::span<const std::string_view> args) {
  long address, value;

  if (args.size() < 2 || !parseHex(std::string(args[0]), address) ||
      !parseHex(std::string(args[1]), value)) {
    return CliError::BadParameter;
  }

  storeBytes(address, 4, value);
  compat::println("Value at 0x{:08x} is 0x{:08x}", address, value);
  return CliError::Success;
}

CliError cmd_view_aa(CliEngine & /*engine*/, std::span<const std::string_view> args) {
  long address, range;

  if (args.size() < 2 || !parseHex(std::string(args[0]), address) ||
      !parseHex(std::string(args[1]), range)) {
    return CliError::BadParameter;
  }

  for (long i = address; i < (address + range); i += 4) {
    long value = 0;
    readInvBytes(i, 4, &value);

    if (!(i & 0x0f)) {
      compat::print("\n\r0x{:08x} : {:08x}", i, value);
    } else {
      compat::print(" 0x{:08x}", value);
    }
  }
  compat::print("\n\r");
  return CliError::Success;
}

CliError cmd_view_w(CliEngine & /*engine*/, std::span<const std::string_view> args) {
  long n, value;

  if (args.empty() || !parseHex(std::string(args[0]), n)) {
    return CliError::BadParameter;
  }

  addrWptrWord(n, &value);
  compat::println("Wptr_n is at 0x{:08x}", value);
  return CliError::Success;
}
} // namespace cli::cmds
