#include "line_reader.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>

namespace cli {

namespace {

bool is_stop_request(const std::string &line) {
  auto first = line.find_first_not_of(" \t\r");
  auto last = line.find_last_not_of(" \t\r");
  return first != std::string::npos && first == last && (line[first] == 'g' || line[first] == 'G');
}

struct LineReader {
  std::mutex mutex;
  std::condition_variable available;
  std::deque<std::string> lines;
  bool ended = false;
  std::atomic<int> stop_requests{0}; // "g" lines in the queue, checked without locking

  LineReader() {
    /* detached: it can stay blocked in getline until the process exits */
    std::thread([this] {
      std::string line;
      while (std::getline(std::cin, line)) {
        std::lock_guard lock(mutex);
        if (is_stop_request(line)) ++stop_requests;
        lines.push_back(std::move(line));
        available.notify_one();
      }
      std::lock_guard lock(mutex);
      ended = true;
      available.notify_one();
    }).detach();
  }
};

LineReader &reader() {
  /* never destroyed, as the detached thread may still use it while the program exits */
  static LineReader *instance = new LineReader;
  return *instance;
}

} // namespace

bool read_line(std::string &line) {
  LineReader &input = reader();
  std::unique_lock lock(input.mutex);
  input.available.wait(lock, [&] { return input.ended || !input.lines.empty(); });
  if (input.lines.empty()) return false;
  line = std::move(input.lines.front());
  input.lines.pop_front();
  if (is_stop_request(line)) --input.stop_requests;
  return true;
}

bool take_stop_request() {
  LineReader &input = reader();
  if (input.stop_requests.load(std::memory_order_relaxed) == 0) return false;
  std::lock_guard lock(input.mutex);
  auto found = std::ranges::find_if(input.lines, is_stop_request);
  if (found == input.lines.end()) return false;
  input.lines.erase(found);
  --input.stop_requests;
  return true;
}

} // namespace cli
