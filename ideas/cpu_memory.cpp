#include <concepts>
#include <cstdint>

// idea: use modern C++ concepts to define interfaces at compile time
// this is different than e.g. IMemoryDevice interface with VIRTUAL
// read() and write() methods with runtime cost
template <typename T>
concept MemoryBus = requires(T bus, uint32_t addr, uint32_t val) {
  { bus.read32(addr) } -> std::same_as<uint32_t>;
  { bus.write32(addr, val) } -> std::same_as<void>;
};

template <MemoryBus Bus> class ST20CPU {
  Bus &memory_bus;
  uint32_t registers[16];
  uint32_t iptr;

public:
  explicit ST20CPU(Bus &bus) : memory_bus(bus), iptr(0) {
  }

  void step() {
    uint32_t instr = memory_bus.read32(iptr);
    // decode and execute...
  }
};
