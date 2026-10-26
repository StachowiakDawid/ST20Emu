#include "main.h"
#include "terminal_util.h"

#include "cli/cli_engine.h"
#include "common/defines.h"
#include "core/cpu/st20.h"
#include "core/memory/memory.h"
#include "board/DSR9500.h"
#include "soc/sti5512/STi5512_Devices.h"
#include "soc/sti5518/STi5518_Devices.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <fstream>
#include <mutex>
#include <system_error>
#include <thread>

#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

uint64_t maxInstr = MAX_UNPROMPTED_INSTR;
uint64_t warnInstr = WARN_UNPROMPTED_INSTR;

void readParms(PARMS *userParms) {
  userParms->nParms = 0;

  std::ifstream parmFp{INI_FILE};
  if (!parmFp) {
    compat::println(stderr, "Cannot open INI file {}\nUsing program defaults", INI_FILE);
    return;
  }

  std::string line;
  while (std::getline(parmFp, line)) {
    std::string_view sv = trim(line);

    // Ignore empty lines and comments
    if (sv.empty() || sv.front() == COMMENT_CHAR) {
      continue;
    }

    auto eqPos = sv.find('=');
    if (eqPos == std::string_view::npos) {
      continue;
    }

    std::string_view key = trim(sv.substr(0, eqPos));
    std::string_view val = trim(sv.substr(eqPos + 1));

    if (key.empty() || val.empty()) {
      continue;
    }

    // Safely copy to the legacy C-struct fixed buffers
    auto copySafe = [](char *dest, std::string_view src, size_t max_size) {
      size_t len = std::min(src.size(), max_size - 1);
      std::memcpy(dest, src.data(), len);
      dest[len] = '\0'; // Ensure null-termination
    };

    copySafe(userParms->parameter[userParms->nParms], key, PARM_SIZE);
    copySafe(userParms->value[userParms->nParms], val, PARM_SIZE);

    userParms->nParms++;

    if (userParms->nParms >= MAX_PARMS) {
      compat::println(stderr, "WARNING: INI file exceeds max parameters ({})", MAX_PARMS);
      break;
    }
  }
}

void st20emuInit(const PARMS *userParms) {
  uint32_t zeroFillStart{1};
  uint32_t zeroFillEnd{0};
  for (int i{0}; i < userParms->nParms; ++i) {
    uint64_t value{0};
    std::string_view valStr{userParms->value[i]};
    int base{10};

    if (valStr.starts_with("0x") || valStr.starts_with("0X")) {
      valStr.remove_prefix(2);
      base = 16;
    }

    auto [ptr, ec] = std::from_chars(valStr.data(), valStr.data() + valStr.size(), value, base);

    if (ec == std::errc{}) {
      if (!std::strcmp(userParms->parameter[i], MAX_UNPROMPTED_INSTR_CH)) {
        maxInstr = value;
      } else if (!std::strcmp(userParms->parameter[i], WARN_UNPROMPTED_INSTR_CH)) {
        warnInstr = value;
      } else if (!std::strcmp(userParms->parameter[i], UNDEFINED_WORD_CH)) {
        undefinedWord = static_cast<uint32_t>(value);
      } else if (!std::strcmp(userParms->parameter[i], ZERO_FILL_START_CH)) {
        zeroFillStart = static_cast<uint32_t>(value);
      } else if (!std::strcmp(userParms->parameter[i], ZERO_FILL_END_CH)) {
        zeroFillEnd = static_cast<uint32_t>(value);
      }
    }
  }

  setZeroFillRange(zeroFillStart, zeroFillEnd);
  compat::println("MAX_UNPROMPTED_INSTR={}\nWARN_UNPROMPTED_INSTR={}\nUNDEFINED_WORD=0x{:08x}",
                  maxInstr, warnInstr, undefinedWord);
  if (zeroFillStart <= zeroFillEnd)
    compat::println("ZERO_FILL=0x{:08x}-0x{:08x}", zeroFillStart, zeroFillEnd);
}

/* serial (ASC) output of the emulated SoC, printed one line at a time as "ASCn> text" */
static bool serialSti5512 = false;
static std::array<std::string, 4> serialLines;

static void printSerialOutput(bool flush) {
  for (int port = 0; port < 4; ++port) {
    std::vector<uint8_t> bytes;
    if (serialSti5512) bytes = takeSti5512AscTransmit(port);
    else if (port == 1) bytes = takeSti5518Asc1Transmit();
    else if (port == 2) bytes = takeSti5518Asc2Transmit();
    std::string &line = serialLines[static_cast<std::size_t>(port)];
    for (uint8_t byte : bytes) {
      if (byte == '\n') {
        compat::println("ASC{}> {}", port, line);
        line.clear();
      } else if (byte != '\r') {
        line.push_back(static_cast<char>(byte));
      }
    }
    if (flush && !line.empty()) {
      compat::println("ASC{}> {}", port, line);
      line.clear();
    }
  }
}

static const char *cpuErrorText(int error) {
  if (error <= ST20_ERROR_START && error >= ST20_ERROR_END) return st20Error(error);
  if (error <= MEMORY_ERROR_START && error >= MEMORY_ERROR_END) return memoryError(error);
  return "Instruction execution failed";
}

struct BatchLogRecord {
  uint64_t count = 0;
  bool executed = true;
  CPUTrace trace;
  std::string diagnostics;
  std::string workspaceDiagnostics;
};

struct BatchLogChunk {
  std::vector<BatchLogRecord> records;
  std::string finalOutput;
};

class BatchLogWriter {
  std::mutex mutex;
  std::condition_variable changed;
  std::deque<BatchLogChunk> pending;
  bool stopping = false;
  int writeError = 0;
  std::thread worker;

  bool writeChunk(const BatchLogChunk &chunk, std::string &buffer) {
    auto flushBuffer = [&] {
      bool written = std::fwrite(buffer.data(), 1, buffer.size(), stdout) == buffer.size();
      buffer.clear();
      return written;
    };
    for (const auto &record : chunk.records) {
      buffer.append(record.diagnostics);
      if (record.executed) {
        compat::println("Executed {} instructions", record.count);
        printNextInstr(record.trace);
      }
      buffer.append(record.workspaceDiagnostics);
      printCPUState(record.trace);
      if (buffer.size() >= 1024 * 1024 && !flushBuffer()) return false;
    }
    buffer.append(chunk.finalOutput);
    return flushBuffer() && std::fflush(stdout) == 0 && !std::ferror(stdout);
  }

  void writeBatches() {
    std::string buffer;
    buffer.reserve(1024 * 1024);
    compat::outputBuffer = &buffer;
    std::unique_lock lock(mutex);
    for (;;) {
      changed.wait(lock, [&] { return stopping || !pending.empty(); });
      if (pending.empty()) {
        compat::outputBuffer = nullptr;
        return;
      }
      BatchLogChunk chunk = std::move(pending.front());
      pending.pop_front();
      changed.notify_all();
      lock.unlock();
      errno = 0;
      bool written = writeChunk(chunk, buffer);
      int error = written ? 0 : (errno ? errno : EIO);
      lock.lock();
      if (error) {
        writeError = error;
        pending.clear();
        changed.notify_all();
        compat::outputBuffer = nullptr;
        return;
      }
    }
  }

public:
  BatchLogWriter() : worker([this] { writeBatches(); }) {}
  ~BatchLogWriter() { finish(); }

  bool submit(BatchLogChunk &chunk) {
    std::unique_lock lock(mutex);
    changed.wait(lock, [&] { return writeError || stopping || pending.size() < 2; });
    if (writeError || stopping) return false;
    if (!chunk.records.empty() || !chunk.finalOutput.empty()) {
      pending.push_back(std::move(chunk));
      chunk = {};
      changed.notify_all();
    }
    return true;
  }

  int finish() {
    {
      std::lock_guard lock(mutex);
      stopping = true;
    }
    changed.notify_all();
    if (worker.joinable()) worker.join();
    return writeError;
  }
};

static int runBatch() {
  constexpr uint64_t LOG_INTERVAL = 100000;
  constexpr uint64_t SERIAL_INTERVAL = 1000;
  std::fflush(stdout);
  BatchLogWriter writer;
  BatchLogChunk chunk;
  std::string logBuffer;
  compat::outputBuffer = &logBuffer;
  uint64_t instrCount = 0;
  for (;;) {
    BatchLogRecord record;
    int result = decodeNextInstr();
    if (result) {
      compat::println("Cannot fetch instruction: {} (code {})", cpuErrorText(result), result);
      record.executed = false;
    } else {
      int watchTripped = 0;
      result = execInstr(&watchTripped);
      ++instrCount;
    }
    if (result || instrCount % SERIAL_INTERVAL == 0) printSerialOutput(result != 0);

    record.count = instrCount;
    record.diagnostics.swap(logBuffer);
    captureCPUTrace(record.trace);
    record.workspaceDiagnostics.swap(logBuffer);
    chunk.records.push_back(std::move(record));
    if (result) {
      if (chunk.records.back().executed) {
        compat::println("Execution stopped: {} (code {})", cpuErrorText(result), result);
      }
      compat::println("Finished after {} instructions", instrCount);
      break;
    }
    if (instrCount % LOG_INTERVAL == 0) {
      if (!writer.submit(chunk)) break;
    }
  }
  compat::outputBuffer = nullptr;
  chunk.finalOutput.swap(logBuffer);
  writer.submit(chunk);
  if (int error = writer.finish()) {
    errno = error;
    std::perror("Cannot write batch log");
  }
  return 1;
}

int main(int argc, char **argv) {
  std::string firmware;
  uint32_t loadAddress = 0;
  bool addressSpecified = false;
  bool batch = false;
  std::string_view soc = "sti5518";
  uint32_t socRevision = 0;
  bool revisionSpecified = false;
  std::string_view board;
  for (int index = 1; index < argc; ++index) {
    std::string_view option = argv[index];
    if (option == "--help" || option == "-h") {
      compat::println("Usage: {} [--soc sti5518|sti5512] [--soc-revision HEX] [--board dsr9500] [--firmware FILE] [--address HEX] [--batch]\n"
              "  --soc NAME       Emulated SoC (default: sti5518)\n"
              "  --soc-revision HEX  STi5512 VID_REV byte (default: synthetic 00)\n"
              "  --board NAME     Board parts outside the SoC: dsr9500 (STi5512 only)\n"
                      "  --firmware FILE  Load raw firmware at startup\n"
                      "  --address HEX    Load and entry address (0/default: auto-place, START_ADDR entry)\n"
                      "  --batch          Run until an error, logging to st20emu-log-{{timestamp}}.txt",
                      argv[0]);
      return 0;
    }
    if (option == "--batch") {
      batch = true;
    } else if (option == "--soc" && index + 1 < argc) {
      soc = argv[++index];
      if (soc != "sti5518" && soc != "sti5512") {
        compat::println(stderr, "Invalid SoC: {} (expected sti5518 or sti5512)", soc);
        return 2;
      }
    } else if (option == "--board" && index + 1 < argc) {
      board = argv[++index];
      if (board != "dsr9500") {
        compat::println(stderr, "Invalid board: {} (expected dsr9500)", board);
        return 2;
      }
    } else if (option == "--soc-revision" && index + 1 < argc) {
      std::string_view value = argv[++index];
      if (value.starts_with("0x") || value.starts_with("0X")) value.remove_prefix(2);
      auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), socRevision, 16);
      if (error != std::errc{} || end != value.data() + value.size() || socRevision > 0xffU) {
        compat::println(stderr, "Invalid SoC revision: {} (expected hexadecimal byte)", argv[index]);
        return 2;
      }
      revisionSpecified = true;
    } else if ((option == "--firmware" || option == "--address") && index + 1 < argc) {
      std::string_view value = argv[++index];
      if (option == "--firmware") {
        firmware = value;
      } else {
        if (value.starts_with("0x") || value.starts_with("0X")) value.remove_prefix(2);
        auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), loadAddress, 16);
        if (error != std::errc{} || end != value.data() + value.size()) {
          compat::println(stderr, "Invalid firmware address: {}", argv[index]);
          return 2;
        }
        addressSpecified = true;
      }
    } else {
      compat::println(stderr, "Unknown option or missing value: {} (use --help)", option);
      return 2;
    }
  }
  if (board == "dsr9500" && soc != "sti5512") {
    compat::println(stderr, "--board dsr9500 requires --soc sti5512");
    return 2;
  }
  if (revisionSpecified && soc != "sti5512") {
    compat::println(stderr, "--soc-revision requires --soc sti5512");
    return 2;
  }
  if ((batch || addressSpecified) && firmware.empty()) {
    compat::println(stderr, "--batch and --address require --firmware FILE");
    return 2;
  }

  if (batch) {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};

#ifdef _WIN32
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif

    char timestamp[32]{};
    std::strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S", &localTime);
    std::string logPath = std::format("st20emu-log-{}.txt", timestamp);
    int logFd = open(logPath.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (logFd < 0) {
      std::perror("Cannot create batch log");
      return 1;
    }
    compat::println("Batch log: {}", logPath);
    std::fflush(stdout);
    if (dup2(logFd, STDOUT_FILENO) < 0 || dup2(logFd, STDERR_FILENO) < 0) {
      std::perror("Cannot redirect batch output");
      close(logFd);
      return 1;
    }
    close(logFd);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    compat::println("Firmware: {} (load address: 0x{:08x}, 0 means auto-place)", firmware, loadAddress);
  }

  // TODO: refactor this to bool when possible
  int watchTripped{0};
  uint64_t instrCount{0};
  // this cannot be true, it just cannot
  // instruction is ready only when the first instruction is fetched, not before the loop
  // otherwise it would start at 0x7ffffffc instead of 0x7ffffffe
  bool instructionReady{false};
  PARMS userParms{};
  cli::CliEngine cli;

  readParms(&userParms);

  st20emuInit(&userParms);
  st20Init(&userParms);
  memoryInit();
  compat::println("SoC: {}", soc);
  if (soc == "sti5512") compat::println("VID_REV: 0x{:02x}", socRevision);
  serialSti5512 = soc == "sti5512";
  int deviceResult = soc == "sti5512" ? initSti5512Devices(static_cast<uint8_t>(socRevision))
                                     : initSti5518Devices();
  if (deviceResult) {
    compat::println(stderr, "Cannot initialize {} devices: {}", soc, memoryError(deviceResult));
    return 1;
  }
  if (board == "dsr9500") {
    compat::println("Board: {}", board);
    if (initDsr9500Board()) {
      compat::println(stderr, "Cannot initialize board {}", board);
      return 1;
    }
  }

  if (!firmware.empty()) {
    uint32_t dataLength = 0;
    int result = bulkLoadBytes(loadAddress, firmware.c_str(), nullptr, &dataLength);
    if (result) {
      compat::println(stderr, "Cannot load firmware: {} (code {})", memoryError(result), result);
      return 1;
    }
    setIptr(loadAddress == 0 ? startAddr : loadAddress);
    compat::println("Read {} bytes from {}", dataLength, firmware);
  }
  if (batch) return runBatch();

  compat::println("");

  // throttle how often we ask the OS for keyboard input
  constexpr uint64_t KEY_POLL_INTERVAL = 1000;
  constexpr uint64_t SERIAL_INTERVAL = 1000;
  uint64_t serialCount = 0;

  while (!cli.is_quit_requested()) {
    if (instructionReady) {
      int result = execInstr(&watchTripped);
      if (result || ++serialCount % SERIAL_INTERVAL == 0) printSerialOutput(result != 0);
      if (result) {
        compat::println("Execution stopped: {}", cpuErrorText(result));
        cli.set_need_prompt(true);
      }
    }

    /*
     * if the program is running without prompts, exit this state if
     * we have executed lots of instructions and haven't encountered
     * a watch condition or if a key has been pressed.
     */
    if (!cli.needs_prompt()) {
      if (++instrCount >= maxInstr) {
        compat::println("We've run {} instr without encountering a watch  curr iptr:{:08x}",
                        instrCount, get_iptr());
        cli.set_need_prompt(true);
      } else if (instrCount % warnInstr == 0) {
        compat::println("Executed {} of {} instr before next prompt  curr iptr: {:08x}", instrCount,
                        maxInstr, get_iptr());
      }

      if (instrCount % KEY_POLL_INTERVAL == 0) {
        if (pollInterruptKey()) {
          compat::println("\nExecution interrupted by user (pressed 'g').");
          cli.set_need_prompt(true);
        }
      }
    }

    if (watchTripped) {
      compat::println("Watch condition encountered");
      cli.set_need_prompt(true);
      watchTripped = false;
    }

    /*
     * print the state of the processor
     * after executing the instruction
     */
    if (cli.needs_prompt()) {
      printCPUState();
    }

    int decodeResult = decodeNextInstr();
    instructionReady = decodeResult == 0;
    if (!instructionReady) {
        compat::println("Cannot fetch instruction: {}", cpuErrorText(decodeResult));
      cli.set_need_prompt(true);
    }

    if (cli.needs_prompt()) {
      if (instructionReady) printNextInstr();

      cli.set_need_cmd(true);
      while (cli.needs_cmd() && cli.needs_prompt()) {
        cli::CliError err = cli.prompt_and_execute();

        if (err != cli::CliError::Success) {
          compat::println(stderr, "ERROR: {}", cli::format_error(err));
        }

        /*
         * if a person wants the program to run without prompting, start
         * a counter to make sure control eventually returns to the user
         */
        if (!cli.needs_prompt()) {
          instrCount = 0;
        }
      }

      if (!cli.is_quit_requested()) instructionReady = decodeNextInstr() == 0;
    }
  }

  compat::println("Finished\n");

  freeAllMemory();

  return 0;
}
