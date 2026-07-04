#include "cli_engine.h"
#include "cli_commands.h"
#include "../utils/compat.h"
#include "../st20.h"

#include <iostream>
#include <sstream>
#include <algorithm>

namespace cli {

const char *format_error(CliError error) {
  switch (error) {
  case CliError::Success:
    return "Success";
  case CliError::GetCommandError:
    return "Can't read command line";
  case CliError::InputError:
    return "Invalid input command or error reading command";
  case CliError::BadParameter:
    return "Command parameter was missing or invalid";
  case CliError::NoCommandFile:
    return "Can't read input command file";
  case CliError::UnknownCommand:
    return "Unknown command";
  case CliError::NoWatchesSet:
    return "Prompting must be enabled if no watch conditions are set";
  }
  return "Unknown error";
}

CliError CliEngine::set_need_prompt(bool flag) {
  need_prompt = flag;
  if (!need_prompt && !anyWatch()) {
    need_prompt = true;
    return CliError::NoWatchesSet;
  }
  return CliError::Success;
}

CliError CliEngine::prompt_and_execute() {
  compat::print("> ");
  std::fflush(stdout);

  std::string line;
  if (!std::getline(std::cin, line)) {
    return CliError::GetCommandError;
  }

  // parse the line into dynamic tokens
  std::istringstream iss{line};
  std::vector<std::string> tokens;
  std::string token;
  while (iss >> token) {
    tokens.push_back(token);
  }

  std::vector<std::string_view> args;
  if (tokens.empty()) {
    return execute_command("", args); // run next instruction
  }

  // convert the command name to lowercase
  std::string cmd_name = tokens.front();
  std::ranges::transform(cmd_name, cmd_name.begin(),
                         [](unsigned char c) { return std::tolower(c); });

  // create string_views for the arguments to avoid copying strings
  args.reserve(tokens.size() - 1);
  for (size_t i = 1; i < tokens.size(); ++i) {
    args.emplace_back(tokens[i]);
  }

  return execute_command(cmd_name, args);
}

CliError CliEngine::execute_command(std::string_view cmd_name,
                                    std::span<const std::string_view> args) {
  auto it = cmd_dictionary.find(cmd_name);
  if (it == cmd_dictionary.end()) {
    return CliError::UnknownCommand;
  }

  // execute the command, passing `this` engine and the dynamic arguments
  return it->second(*this, args);
}

} // namespace cli
