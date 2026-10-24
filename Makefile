TARGET = st20emu
OBJ_DIR = obj
SRC_DIR = src

CC = clang
CXX = clang++

SRCS_CPP = $(shell find $(SRC_DIR) -type f -name '*.cpp')
SRCS_C   = $(shell find $(SRC_DIR) -type f -name '*.c')

# e.g., src/cli/cmds/control.cpp -> obj/cli/cmds/control.o
OBJS_CPP = $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SRCS_CPP))
OBJS_C   = $(patsubst $(SRC_DIR)/%.c,   $(OBJ_DIR)/%.o, $(SRCS_C))
OBJS     = $(OBJS_CPP) $(OBJS_C)

CPU_TEST = $(OBJ_DIR)/tests/cpu-instructions
CPU_TEST_SRCS = tests/cpu_instructions.cpp src/core/cpu/instructions.cpp src/core/cpu/st20.cpp src/core/cpu/instrentry.cpp src/core/memory/memory.cpp src/soc/sti5512/STi5512_Devices.cpp src/soc/sti5518/STi5518_Devices.cpp
CPU_TEST_HEADERS = $(wildcard src/core/cpu/*.h src/core/memory/*.h src/common/*.h src/soc/sti5512/STi5512_Devices.h src/soc/sti5518/STi5518_Devices.h)

CFLAGS_BASE   = -std=c23
CXXFLAGS_BASE = -std=c++23 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wold-style-cast -pthread
LDFLAGS_BASE  = -pthread

BUILD ?= debug

ifeq ($(BUILD), release)
  # separate objects and binary so release and debug builds don't overwrite each other
  OBJ_DIR  = obj/release
  TARGET   = st20emu-release
  CFLAGS   = $(CFLAGS_BASE) -O3 -DNDEBUG -flto
  CXXFLAGS = $(CXXFLAGS_BASE) -O3 -DNDEBUG -flto
  LDFLAGS  = $(LDFLAGS_BASE) -O3 -flto
  MSG      = "Building in RELEASE mode..."
else
  CFLAGS   = $(CFLAGS_BASE) -O0 -g -fsanitize=address,undefined
  CXXFLAGS = $(CXXFLAGS_BASE) -O0 -g -fsanitize=address,undefined
  LDFLAGS  = $(LDFLAGS_BASE) -fsanitize=address,undefined
  MSG      = "Building in DEBUG mode with sanitizers..."
endif

all: $(TARGET)

release:
	$(MAKE) BUILD=release

test: $(CPU_TEST)
	./$(CPU_TEST)

test-st20sim: $(CPU_TEST)
	node tests/st20sim_differential.js $(ST20SIM_ARGS)

$(CPU_TEST): $(CPU_TEST_SRCS) $(CPU_TEST_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPU_TEST_SRCS) -o $@ $(LDFLAGS)

$(filter $(OBJ_DIR)/core/cpu/%.o,$(OBJS_CPP)): $(CPU_TEST_HEADERS)

$(OBJS_CPP): $(wildcard $(SRC_DIR)/common/*.h)

$(TARGET): $(OBJS)
	@echo $(MSG)
	@echo "Linking $(TARGET)..."
	$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling C++ file $<..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling C file $<..."
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	@echo "Cleaning up build artifacts..."
	rm -rf $(OBJ_DIR) $(TARGET)

.PHONY: all release clean test test-st20sim
