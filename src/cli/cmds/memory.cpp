#include "../cli_commands.h"

#include "../../common/compat.h"
#include "../../core/cpu/st20.h"
#include "../../core/memory/memory.h"

#include <fstream>
#include <string>

#include <cstdint>

namespace cli::cmds {
CliError cmd_load(CliEngine & /*engine*/, std::span<const std::string_view> args) {
  if (args.empty())
    return CliError::BadParameter;

  std::string file_path(args[0]);
  int result = loadMemory(file_path.c_str());
  if (result == 0)
    result = loadCPUState(file_path.c_str());

  printCPUState();
  return result == 0 ? CliError::Success : CliError::InputError;
}

CliError cmd_save(CliEngine & /*engine*/, std::span<const std::string_view> args) {
  if (args.empty())
    return CliError::BadParameter;

  std::string file_path(args[0]);
  int result = saveMemory(file_path.c_str());
  if (result == 0) {
    result = saveCPUState(file_path.c_str());
  }

  return result == 0 ? CliError::Success : CliError::InputError;
}

/* dump FILE ADDRESS LENGTH: write memory as raw bytes (unwritten bytes as 0) */
CliError cmd_dump(CliEngine & /*engine*/, std::span<const std::string_view> args) {
  uint32_t address, length;
  if (args.size() < 3 || !parseHex(std::string(args[1]), address) ||
      !parseHex(std::string(args[2]), length))
    return CliError::BadParameter;
  std::ofstream out{std::string(args[0]), std::ios::binary};
  for (uint32_t offset = 0; offset < length; ++offset) {
    uint32_t value = 0;
    if (readBytes(address + offset, 1, &value)) value = 0;
    out.put(static_cast<char>(value));
  }
  return out ? CliError::Success : CliError::InputError;
}

CliError cmd_view(CliEngine & /*engine*/, std::span<const std::string_view> args) {
  uint32_t address;
  uint32_t value = 0;

  if (args.empty() || !parseHex(std::string(args[0]), address))
    return CliError::BadParameter;

  readBytes(address, 4, &value);
  compat::println("Value at 0x{:08x} is 0x{:08x}", address, value);
  return CliError::Success;
}

CliError cmd_view_a(CliEngine & /*engine*/, std::span<const std::string_view> args) {
  uint32_t address, value;

  if (args.size() < 2 || !parseHex(std::string(args[0]), address) ||
      !parseHex(std::string(args[1]), value)) {
    return CliError::BadParameter;
  }

  storeBytes(address, 4, value);
  compat::println("Value at 0x{:08x} is 0x{:08x}", address, value);
  return CliError::Success;
}

CliError cmd_view_aa(CliEngine & /*engine*/, std::span<const std::string_view> args) {
  uint32_t address, range;

  if (args.size() < 2 || !parseHex(std::string(args[0]), address) ||
      !parseHex(std::string(args[1]), range)) {
    return CliError::BadParameter;
  }

  for (uint64_t offset = 0; offset < range; offset += BytesPerWord) {
    uint32_t i = address + static_cast<uint32_t>(offset);
    uint32_t value = 0;
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
  uint32_t n, value;

  if (args.empty() || !parseHex(std::string(args[0]), n)) {
    return CliError::BadParameter;
  }

  addrWptrWord(n, &value);
  compat::println("Wptr_n is at 0x{:08x}", value);
  return CliError::Success;
}
} // namespace cli::cmds
