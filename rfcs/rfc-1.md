# RFC 1

**Status**: In Progress
**Last Updated**: 14 July 2026
**Created on**: 14 July 2026
**Created by**: Michał Korczak

## Overview

The core principle of this project is separation of concerns.
The ST20 is an instruction set architecture (ISA), while chips like STi5518 are System-on-Chips (SoCs) that embed an ST20 core alongside specific peripherals, memory maps, and hardware registers.

To ensure future extensibility, the CPU must know absolutely nothing about the SoC, and the core emulation libraries must know absolutely nothing about how they are being rendered to the screen (CLI or GUI).

This is not the case with the current project state. In example, CPU emulation calls CLI functions directly.

## Component structure

The project should be broken down into shared libraries (either static or dynamic, managed via Meson) and executables (preferably lightweight).

- `libst20core` - the CPU engine:
  - Contains the `ST20CPU` class, register state, and the pure execution loop.
  - Defines virtual interfaces (or modern `concepts`) like `MemoryBus` that the CPU uses to read/write bytes. The CPU does not know *what* it is reading from.
- `libst20dis` - the disassembler:
  - A standalone library responsible purely for decoding a byte stream into human-readable mnemonics.
  - Shared universally across the CLI disassembler tool, the GUI debugger, and the CPU execution tracer.
- `libst20soc` / `libstisoc` - the hardware layer:
  - Implements the `MemoryBus` interface.
  - Contains the specific address maps and peripheral logic for the STi5518, STi5512, etc.
  (e.g., routing a write to `0x20000000` to the simulated video decoder instead of RAM).
  - Name could follow the `st20` style as there are more SoCs than *STixxxx* like *ST20TP2*.
- Frontends:
  - `st20cli`: Command line runner with possible interactivity via FTXUI. Could have additional features like argument options and be used for more sophisticated CI/CD testing.
  - `st20gui`: The immediate mode UI application (ImGui or Clay with SDL backend). Wraps the core, provides memory viewers, disassembler views (via `libst20dis`), stepping controls, and video output. Look at the [Gearboy](https://github.com/drhelius/Gearboy) project for a visual example.
  - `st20disasm`: A command-line tool that acts as a modern replacement for the closed-source (although community-created) `st20dis`.

### Proposed directory structure

```plaintext
├── meson.build
├── core/
│   ├── cpu/           # libst20core (registers, dispatcher, execution)
│   ├── disasm/        # libst20dis (byte-to-string decoding)
│   └── soc/           # libst20soc (STi5518 memory map, peripherals)
├── frontends/
│   ├── cli/           # st20cli
│   ├── gui/           # st20gui
│   └── disasm/        # st20disasm
└── tests/             # unit tests for instruction logic
```

This architecture allows to reuse the disassembler everywhere, and treats the CPU as a black box that just processes bytes from an abstract bus.

## Emulating OS20

In reality, software compiled with `st20cc -runtime os20` embeds the OS20 real-time kernel directly into the binary. OS20 utilizes ST20 hardware features (like the high/low priority shadow registers, hardware timers, and process queues) to perform mutli-tasking.

We should explicitly **not** try to emulate OS20 software calls directly (this is called a high-level emulation). Because OS20 relies heavily on the ST20's hardware task scheduler, if we accurately emulate the ST20 core registers, timers, and interrupts, the OS20 kernel embedded in the target binary will boot and run itself naturally. The emulator simply acts as the silicon at this point.

## Some considerations for this proposal

### A standalone disassembler

Building `libst20dis` first actually gives us the perfect testing tool for `libst20core`. If we can disassemble a ROM perfectly, we know the instruction decoding logic is flawless.

Right now, there is no possibility to test our disassembler as it is embeded into the emulation execution loop.

### GUI

Immediate mode UI is the industry standard for emulator frontends (and gamedev). There is no objection to this. It allows to build memory viewer, register inspectors, and active disassembly windows.

For starters, we should not try to build a Gearboy-style GUI. A huge advancement will be a clear video output of our most-awaited emulation.

### Testing

At some point, we need to build an automated test harness. This harness should test CPU logic and assert final registers states. The ST20 architecture is not our usual ISA and should be tested thoroughly

### Compiler and linker

There is no point in rewriting `st20cc`, `st20libr`, or the `st20link` linker. Building a C compiler for an obscure 32-bit architecture is a massive and impossible for us project on its own. We should assume the user already has a compiled binary as the 90' ended some time ago. Although, finding toolchain executables could provide helpful for later debugging process.

### Monolithic SoC emulation

Hardcoding STi5518 video/audio registers directly into the ST20 CPU class will make it impossible to support the STi5512 later. At this moment, we do not have the knowledge of the extent of needed support for this kind of stuff.

It is possible to find common patterns between these huge register manuals. During the advanced stage of this project we could redefine the structure to YAML files or keep it in the `.cpp` source code as we get to know more about the shared intricacies of ST20 SoCs.

## Meson build system

The proposed stack is: `mise` + `meson` (with `ninja` and `mold`) + `just`. Meson is the most modern build system, not only for C++ projects, that we can use along with `ninja` builder and `mold` linker. With `mise` we can ensure reproducible environments, and `just` is just a simpler `make`.

### `mise.toml` - reproducible environments

A simple example of an environment with a block for tooling versions newer (younger) than 7 days and a lockfile.

```toml
[settings]
lockfile = true
minimum_release_age = "7d"

[tools]
clang = "22"
clang-format = "22"
meson = "latest"
ninja = "latest"
just = "latest"
mold = "latest"

[env]
CC = "clang"
CXX = "clang++"
```

### `meson` - build architecture

`meson.build` containing basic configuration with `mold` linker and project subdirectories.

```meson
project('st20emu', 'cpp',
  version: '0.1.0',
  default_options: [
    'cpp_std=c++23',
    'optimization=3',
    'warning_level=3'
  ]
)

compiler = meson.get_compiler('cpp')
if compiler.has_link_argument('-fuse-ld=mold')
  add_project_link_arguments('-fuse-ld=mold', language: ['c', 'cpp'])
endif

# global include directories (if we would have headers separated from src)
inc = include_directories('.')

subdir('core')
subdir('frontends')
subdir('tests')
```

---

`core/meson.build` example

```meson
# build the core libraries, order matters if they depend on each other
subdir('disasm')
subdir('cpu')
subdir('soc')
```

---

`core/cpu/meson.build` example

```meson
core_sources = [
  'st20cpu.cpp',
  # ... other cpu source files ...
]

libst20core = static_library('st20core',
  core_sources,
  include_directories : inc
)

# this dependency object is what frontends will link against
# it automatically provides the library AND the include paths
st20core_dep = declare_dependency(
  link_with : libst20core,
  include_directories : inc
)
```

---

`frontends/cli/meson.build` example

```meson
cli_sources = [
  'main.cpp'
]

executable('st20cli',
  cli_sources,
  dependencies : [st20core_dep, st20soc_dep, st20dis_dep],
  install : true
)
```

### `just` - task runner

`justfile` example

```just
set shell := ["bash", "-c"]

default: build

setup:
    meson setup builddir

build:
    meson compile -C builddir

run-cli rom_path="": build
    ./builddir/frontends/cli/st20cli {{rom_path}}

run-disasm rom_path="": build
    ./builddir/frontends/disasm/st20disasm {{rom_path}}

test: build
    meson test -C builddir -v

clean:
    rm -rf builddir
```

## Architecture example

### Memory bus

We could define a memory bus interface like below using virtual functions:

```cpp
#pragma once

#include <cstdint>
#include <expected>

namespace core::cpu {

// defines potential hardware-level faults that can occur on the bus
enum class MemoryError {
    Success,
    UnalignedAccess,
    BusFault,       // e.g., accessing completely unmapped memory
    ReadOnlyFault   // e.g., attempting to write to ROM
};

class IMemoryBus {
public:
    virtual ~IMemoryBus() = default;

    // --- 8-bit access ---
    // byte accesses are always aligned
    virtual std::expected<uint8_t, MemoryError> read_byte(int32_t address) = 0;
    virtual MemoryError write_byte(int32_t address, uint8_t value) = 0;

    // --- 16-bit access (half-word) ---
    // hardware enforces that bit 0 of the address must be 0.
    virtual std::expected<uint16_t, MemoryError> read_half(int32_t address) = 0;
    virtual MemoryError write_half(int32_t address, uint16_t value) = 0;

    // --- 32-bit access (word) ---
    // hardware enforces that bits 0 and 1 of the address must be 0.
    virtual std::expected<uint32_t, MemoryError> read_word(int32_t address) = 0;
    virtual MemoryError write_word(int32_t address, uint32_t value) = 0;
};

} // namespace core::cpu
```

---

The alternative are modern `concepts` without a runtime cost of `virtual` OOP.
It will take some time to learn this new concept. Virtual functions should be avoided as a cursed OOP-thingy.

The example below is not 1 to 1 reimplementation of above, but one could be provided later:

```cpp
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
```

### IO

Just an idea for using `variants`, nothing is confirmed here:

```cpp
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
```
