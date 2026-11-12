#include "main.h"

#include "cli/cli_engine.h"
#include "cli/line_reader.h"
#include "console/serial_console.h"
#include "console/serial_tcp.h"
#include "core/cpu/omr.h"
#include "core/cpu/st20.h"
#include "core/memory/memory.h"
#include "board/DSR7000.h"
#include "board/DSR9500.h"
#include "board/Stb6300.h"
#include "soc/sti5512/STi5512_Devices.h"
#include "soc/sti5518/STi5518_Devices.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <mutex>
#include <system_error>
#include <thread>

#include <cstdint>
#include <cstdio>
#include <cstring>

constexpr uint64_t maxInstr = MAX_UNPROMPTED_INSTR;
constexpr uint64_t warnInstr = WARN_UNPROMPTED_INSTR;

/* serial (ASC) output of the emulated SoC, printed one line at a time as "ASCn> text", except
 * the --console port's, which goes to stdout unchanged */
static bool serialSti5512 = false;
static int consolePort = -1;
static int tcpSerialPort = -1; // --serial-tcp PORT:TCP
static std::array<std::string, 4> serialLines;

/* --serial-tcp: the client's bytes go to the port's receive queue; polled every 16th output
 * check (16,000 instructions) */
static void pollSerialTcp() {
  static unsigned calls = 0;
  if (tcpSerialPort < 0 || ++calls % 16 != 0) return;
  std::vector<uint8_t> received;
  serialtcp::poll(received);
  if (received.empty()) return;
  if (serialSti5512) sti5512Peripherals().feedAscReceive(tcpSerialPort, received);
  else sti5518Peripherals().feedAscReceive(tcpSerialPort, received);
}

static void printSerialOutput(bool flush) {
  pollSerialTcp();
  for (int port = 0; port < 4; ++port) {
    std::vector<uint8_t> bytes;
    if (serialSti5512) bytes = sti5512Peripherals().takeAscTransmit(port);
    else bytes = sti5518Peripherals().takeAscTransmit(port);
    if (port == tcpSerialPort && !bytes.empty() && serialtcp::send(bytes)) continue;
    if (port == consolePort) {
      if (!bytes.empty()) {
        std::fwrite(bytes.data(), 1, bytes.size(), stdout);
        std::fflush(stdout);
      }
      continue;
    }
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

extern CPUSTATE cpuState;
extern OMRSTATE omrState;

static void printRunSummary(uint64_t executed, std::chrono::steady_clock::time_point start) {
  double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  compat::println("Executed {} instructions in {:.2f} s ({:.2f} MIPS)", executed, seconds,
                  static_cast<double>(executed) / seconds / 1e6);
  compat::println("A={:08x} B={:08x} C={:08x} Iptr={:08x} Wptr={:08x} Priority={} ClockHP={:08x} "
                  "ClockLP={:08x} Cycles={}",
                  cpuState.areg, cpuState.breg, cpuState.creg, cpuState.iptr, cpuState.wptr, cpuState.priority,
                  omrState.ClockRegHP, omrState.ClockRegLP, elapsedCpuCycles());
}

/* free-runs count instructions without the prompt (benchmarks, the profile-guided build's
 * training run), then reports the speed */
static int runFree(uint64_t count) {
  compat::notesEnabled = false;
  int result = decodeNextInstr();
  if (result) {
    compat::println("Cannot fetch instruction: {}", cpuErrorText(result));
    return 1;
  }
  auto start = std::chrono::steady_clock::now();
  uint64_t executed = 0;
  while (executed < count) {
    RunResult run = runInstructions(std::min<uint64_t>(count - executed, 1000));
    executed += run.completed;
    if (run.stopped) {
      ++executed;
      result = run.execResult ? run.execResult : decodeNextInstr();
      printSerialOutput(true);
      if (run.execResult) compat::println("Execution stopped: {}", cpuErrorText(result));
      else if (result) compat::println("Cannot fetch instruction: {}", cpuErrorText(result));
      else compat::println("Watch condition encountered");
      break;
    }
    printSerialOutput(false);
  }
  printSerialOutput(true);
  printRunSummary(executed, start);
  return result ? 1 : 0;
}

/* --console PORT: free-runs (up to limit instructions when nonzero) with the host terminal as
 * the port's serial terminal: typed bytes go to its receive queue, its output to stdout raw.
 * Ctrl-] ends the session. */
static int runConsole(int port, uint64_t limit) {
  constexpr uint64_t CHUNK = 1000;           // instructions between serial output checks
  constexpr uint64_t INPUT_POLL_CHUNKS = 100; // chunks between stdin polls
  compat::notesEnabled = false;
  consolePort = port;
  int result = decodeNextInstr();
  if (result) {
    compat::println("Cannot fetch instruction: {}", cpuErrorText(result));
    return 1;
  }
  compat::println("Console on ASC{}: press Ctrl-] to quit", port);
  std::fflush(stdout);
  console::beginSession();
  auto start = std::chrono::steady_clock::now();
  uint64_t executed = 0;
  uint64_t chunks = 0;
  std::vector<uint8_t> input;
  bool quit = false;
  while (!quit && (limit == 0 || executed < limit)) {
    RunResult run = runInstructions(limit ? std::min(limit - executed, CHUNK) : CHUNK);
    executed += run.completed;
    if (run.stopped) {
      ++executed;
      result = run.execResult ? run.execResult : decodeNextInstr();
      printSerialOutput(true);
      if (run.execResult) compat::println("\nExecution stopped: {}", cpuErrorText(result));
      else if (result) compat::println("\nCannot fetch instruction: {}", cpuErrorText(result));
      else compat::println("\nWatch condition encountered");
      break;
    }
    printSerialOutput(false);
    if (++chunks % INPUT_POLL_CHUNKS == 0) {
      console::Input polled = console::pollInput(input);
      quit = polled == console::Input::Escape;
      if (!input.empty()) {
        if (serialSti5512) sti5512Peripherals().feedAscReceive(port, input);
        else sti5518Peripherals().feedAscReceive(port, input);
        input.clear();
      }
    }
  }
  printSerialOutput(true);
  console::endSession();
  compat::println("");
  printRunSummary(executed, start);
  return result ? 1 : 0;
}

/* --script FILE: each line is `run N` (free-run N instructions, as --run) or a CLI command
 * (screen FILE, ir KEY, fp KEY, led, dump ...); `#` starts a comment. Stops at the first run
 * that ends early. */
static int runScript(const std::string &path, cli::CliEngine &cli) {
  std::FILE *file = std::fopen(path.c_str(), "r");
  if (!file) {
    std::perror("Cannot open script");
    return 1;
  }
  char buffer[512];
  int status = 0;
  while (status == 0 && std::fgets(buffer, sizeof(buffer), file)) {
    std::string line = buffer;
    if (auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
    std::size_t first = line.find_first_not_of(" \t");
    if (first == std::string::npos) continue;
    line.erase(0, first);
    compat::println("script> {}", line);
    if (line.starts_with("run ")) {
      uint64_t count = 0;
      std::string_view value = std::string_view(line).substr(4);
      auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), count);
      if (error != std::errc{} || end != value.data() + value.size() || count == 0) {
        compat::println(stderr, "Invalid run count: {}", value);
        status = 2;
      } else {
        status = runFree(count);
      }
    } else {
      cli.execute_line(line);
    }
  }
  std::fclose(file);
  return status;
}

int main(int argc, char **argv) {
  std::string firmware;
  uint32_t loadAddress = 0;
  bool addressSpecified = false;
  bool batch = false;
  uint64_t runCount = 0;
  std::string script;
  int consoleOption = -1;
  std::string_view soc = "sti5518";
  uint32_t socRevision = 0;
  bool revisionSpecified = false;
  std::string_view board;
  for (int index = 1; index < argc; ++index) {
    std::string_view option = argv[index];
    if (option == "--help" || option == "-h") {
      compat::println("Usage: {} [--soc sti5518|sti5512] [--soc-revision HEX] [--board dsr9500|dsr7000|6300] [--firmware FILE] [--address HEX] [--batch] [--run COUNT] [--script FILE] [--console PORT] [--serial-tcp PORT:TCP] [--no-fast-path]\n"
              "  --soc NAME       Emulated SoC (default: sti5518)\n"
              "  --soc-revision HEX  STi5512 VID_REV byte (default: synthetic 00)\n"
              "  --board NAME     Board parts outside the SoC: dsr9500, dsr7000 (STi5512 only), 6300 (STi5518 only)\n"
                      "  --firmware FILE  Load raw firmware at startup\n"
                      "  --address HEX    Load and entry address (0/default: auto-place, START_ADDR entry)\n"
                      "  --batch          Run until an error, logging to st20emu-log-{{timestamp}}.txt\n"
                      "  --run COUNT      Run COUNT instructions without the prompt, then report the speed\n"
              "  --script FILE    Run FILE: lines `run N` and CLI commands (screen FILE, ir KEY, ...)\n"
              "  --console PORT   Run freely with the terminal as ASC PORT (0-3)'s serial console (Ctrl-] quits);\n"
              "                   with --run COUNT, stop after COUNT instructions\n"
              "  --serial-tcp PORT:TCP  Serve ASC PORT's serial line on 127.0.0.1:TCP (raw bytes both ways\n"
              "                   while a client is connected, e.g. SLIP)\n"
                      "  --no-fast-path   Take the full timer/scheduler path for every instruction (reference)",
                      argv[0]);
      return 0;
    }
    if (option == "--batch") {
      batch = true;
    } else if (option == "--no-fast-path") {
      setFastPath(false);
    } else if (option == "--run" && index + 1 < argc) {
      std::string_view value = argv[++index];
      auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), runCount);
      if (error != std::errc{} || end != value.data() + value.size() || runCount == 0) {
        compat::println(stderr, "Invalid instruction count: {}", value);
        return 2;
      }
    } else if (option == "--console" && index + 1 < argc) {
      std::string_view value = argv[++index];
      if (value.size() != 1 || value[0] < '0' || value[0] > '3') {
        compat::println(stderr, "Invalid console port: {} (expected 0-3)", value);
        return 2;
      }
      consoleOption = value[0] - '0';
    } else if (option == "--serial-tcp" && index + 1 < argc) {
      uint16_t tcpPort = 0;
      if (!serialtcp::parseSpec(argv[++index], tcpSerialPort, tcpPort)) {
        compat::println(stderr, "Invalid --serial-tcp: {} (expected PORT:TCP, e.g. 3:5555)", argv[index]);
        return 2;
      }
      if (!serialtcp::listen(tcpPort)) return 1;
      compat::println("ASC{} serial line on 127.0.0.1:{}", tcpSerialPort, serialtcp::boundPort());
    } else if (option == "--script" && index + 1 < argc) {
      script = argv[++index];
    } else if (option == "--soc" && index + 1 < argc) {
      soc = argv[++index];
      if (soc != "sti5518" && soc != "sti5512") {
        compat::println(stderr, "Invalid SoC: {} (expected sti5518 or sti5512)", soc);
        return 2;
      }
    } else if (option == "--board" && index + 1 < argc) {
      board = argv[++index];
      if (board != "dsr9500" && board != "dsr7000" && board != "6300") {
        compat::println(stderr, "Invalid board: {} (expected dsr9500, dsr7000 or 6300)", board);
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
  if (board == "dsr7000" && soc != "sti5512") {
    compat::println(stderr, "--board dsr7000 requires --soc sti5512");
    return 2;
  }
  if (board == "6300" && soc != "sti5518") {
    compat::println(stderr, "--board 6300 requires --soc sti5518");
    return 2;
  }
  if (revisionSpecified && soc != "sti5512") {
    compat::println(stderr, "--soc-revision requires --soc sti5512");
    return 2;
  }
  if ((batch || runCount || addressSpecified || !script.empty() || consoleOption >= 0) && firmware.empty()) {
    compat::println(stderr, "--batch, --run, --script, --console and --address require --firmware FILE");
    return 2;
  }
  if (batch && runCount) {
    compat::println(stderr, "--batch and --run can't be combined");
    return 2;
  }
  if (tcpSerialPort >= 0 && tcpSerialPort == consoleOption) {
    compat::println(stderr, "--serial-tcp and --console can't use the same port");
    return 2;
  }
  if (consoleOption >= 0 && (batch || !script.empty())) {
    compat::println(stderr, "--console can't be combined with --batch or --script");
    return 2;
  }

  if (batch) {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    /* std::localtime's shared result is safe here: no other thread runs yet */
    char timestamp[32]{};
    std::strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S", std::localtime(&time));
    std::string logPath = std::format("st20emu-log-{}.txt", timestamp);
    /* "x" fails if the log exists; both streams then append to it, so neither overwrites the other */
    std::FILE *created = std::fopen(logPath.c_str(), "wx");
    if (!created) {
      std::perror("Cannot create batch log");
      return 1;
    }
    std::fclose(created);
    compat::println("Batch log: {}", logPath);
    std::fflush(stdout);
    if (!std::freopen(logPath.c_str(), "a", stdout) || !std::freopen(logPath.c_str(), "a", stderr)) {
      std::perror("Cannot redirect batch output");
      return 1;
    }
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
  cli::CliEngine cli;

  st20Init();
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
  if (!board.empty()) {
    compat::println("Board: {}", board);
    if (board == "dsr9500" ? initDsr9500Board() : board == "dsr7000" ? initDsr7000Board() : initStb6300Board()) {
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
  if (!script.empty()) return runScript(script, cli);
  if (consoleOption >= 0) return runConsole(consoleOption, runCount);
  if (runCount) return runFree(runCount);

  compat::println("");

  // how often a free run checks for a "g" line that stops it
  constexpr uint64_t KEY_POLL_INTERVAL = 100000;
  constexpr uint64_t SERIAL_INTERVAL = 1000;
  uint64_t serialCount = 0;
  uint64_t warnCountdown = warnInstr; // counts down instead of a division every instruction

  while (!cli.is_quit_requested()) {
    /*
     * free run: execute straight through to the next instruction this loop has to look at
     * (serial output, progress message, stop-request check, instruction limit). An instruction that
     * stops the run is handed to the regular path below as already executed.
     */
    bool executed = false;
    int result = 0;
    if (instructionReady && !cli.needs_prompt()) {
      uint64_t quiet = std::min({SERIAL_INTERVAL - serialCount % SERIAL_INTERVAL, maxInstr - instrCount,
                                 warnCountdown, KEY_POLL_INTERVAL - instrCount % KEY_POLL_INTERVAL});
      if (quiet > 1) {
        compat::notesEnabled = false;
        RunResult run = runInstructions(quiet - 1);
        serialCount += run.completed;
        instrCount += run.completed;
        warnCountdown -= run.completed;
        if (!run.stopped) continue;
        executed = true;
        result = run.execResult;
        watchTripped = run.watchTripped;
      }
    }

    if (instructionReady) {
      if (!executed) {
        compat::notesEnabled = cli.needs_prompt();
        result = execInstr(&watchTripped);
      }
      if (result || ++serialCount % SERIAL_INTERVAL == 0) printSerialOutput(result != 0);
      if (result) {
        compat::println("Execution stopped: {}", cpuErrorText(result));
        cli.set_need_prompt(true);
      }
    }

    /*
     * if the program is running without prompts, exit this state if
     * we have executed lots of instructions and haven't encountered
     * a watch condition or if "g" has been entered.
     */
    if (!cli.needs_prompt()) {
      if (++instrCount >= maxInstr) {
        compat::println("We've run {} instr without encountering a watch  curr iptr:{:08x}",
                        instrCount, get_iptr());
        cli.set_need_prompt(true);
      } else if (--warnCountdown == 0) {
        warnCountdown = warnInstr;
        compat::println("Executed {} of {} instr before next prompt  curr iptr: {:08x}", instrCount,
                        maxInstr, get_iptr());
      }

      if (instrCount % KEY_POLL_INTERVAL == 0) {
        if (cli::take_stop_request()) {
          compat::println("\nExecution interrupted by user (entered 'g').");
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
          warnCountdown = warnInstr;
        }
      }

      if (!cli.is_quit_requested()) instructionReady = decodeNextInstr() == 0;
    }
  }

  compat::println("Finished\n");

  freeAllMemory();

  return 0;
}
