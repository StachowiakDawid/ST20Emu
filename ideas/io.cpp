#include <variant>
#include <vector>

class RAM { /* ... */
};
class ROM { /* ... */
};
class TimerDev { /* ... */
};

// idea: maybe let's use variants as a cache-friendly alternative to inheritance
// it could be better for flat memory layout than an inherited generic Device class

// we'll revisit these ideas when rewriting old code into proper emulation process

// a memory region can be exactly one of these things
using HardwareDevice = std::variant<RAM, ROM, TimerDev>;

class EmulatorBus {
  // map addresses to specific devices
  std::vector<HardwareDevice> hardware_map;
  // ...
};
