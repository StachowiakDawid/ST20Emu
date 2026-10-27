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
CPU_TEST_SRCS = tests/cpu_instructions.cpp src/core/cpu/instructions.cpp src/core/cpu/st20.cpp src/core/cpu/instrentry.cpp src/core/memory/memory.cpp src/soc/common/STi55xx_Asc.cpp src/soc/common/STi55xx_Inc.cpp src/soc/common/STi55xx_Ssc.cpp src/soc/sti5512/STi5512_Devices.cpp src/soc/sti5512/STi5512_Display.cpp src/soc/sti5512/STi5512_Pio.cpp src/soc/sti5512/STi5512_PtiDma.cpp src/soc/sti5512/STi5512_Pwm.cpp src/soc/sti5512/STi5512_RegisterBlocks.cpp src/soc/sti5512/STi5512_VideoDecoder.cpp src/soc/sti5518/STi5518_Devices.cpp src/board/DSR9500.cpp
CPU_TEST_HEADERS = $(wildcard src/core/cpu/*.h src/core/memory/*.h src/common/*.h src/soc/common/*.h src/soc/sti5512/*.h src/soc/sti5518/STi5518_Devices.h src/board/*.h)

CFLAGS_BASE   = -std=c23
CXXFLAGS_BASE = -std=c++23 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wold-style-cast -pthread
LDFLAGS_BASE  = -pthread

BUILD ?= debug

ifeq ($(BUILD), release)
  # separate objects and binary so release and debug builds don't overwrite each other
  OBJ_DIR  = obj/release
  TARGET   = st20emu-release
  CFLAGS   = $(CFLAGS_BASE) -O3 -DNDEBUG -flto $(PGO_FLAGS)
  CXXFLAGS = $(CXXFLAGS_BASE) -O3 -DNDEBUG -flto $(PGO_FLAGS)
  LDFLAGS  = $(LDFLAGS_BASE) -O3 -flto $(PGO_FLAGS)
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

# profile-guided release build: an instrumented build boots the DSR9500 firmware, and the
# profile it records steers inlining and branch layout of st20emu-pgo
PGO_DIR       = obj/pgo
PGO_PROFILE   = $(PGO_DIR)/st20emu.profdata
PGO_TRAINING  = --soc sti5512 --board dsr9500 --firmware firmware/dsr9500mcmain.bin --run 200000000
LLVM_PROFDATA ?= $(shell command -v llvm-profdata || echo xcrun llvm-profdata)

pgo:
	$(MAKE) BUILD=release OBJ_DIR=$(PGO_DIR)/generate TARGET=$(PGO_DIR)/st20emu-instrumented PGO_FLAGS=-fprofile-instr-generate
	rm -f $(PGO_DIR)/*.profraw
	LLVM_PROFILE_FILE=$(PGO_DIR)/st20emu-%p.profraw ./$(PGO_DIR)/st20emu-instrumented $(PGO_TRAINING) > $(PGO_DIR)/training.log
	$(LLVM_PROFDATA) merge -output=$(PGO_PROFILE) $(PGO_DIR)/*.profraw
	rm -rf $(PGO_DIR)/use
	$(MAKE) BUILD=release OBJ_DIR=$(PGO_DIR)/use TARGET=st20emu-pgo PGO_FLAGS=-fprofile-instr-use=$(PGO_PROFILE)

test: $(CPU_TEST)
	./$(CPU_TEST)

test-st20sim: $(CPU_TEST)
	node tests/st20sim_differential.js $(ST20SIM_ARGS)

$(CPU_TEST): $(CPU_TEST_SRCS) $(CPU_TEST_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPU_TEST_SRCS) -o $@ $(LDFLAGS)

$(filter $(OBJ_DIR)/core/cpu/%.o,$(OBJS_CPP)): $(CPU_TEST_HEADERS)

$(OBJS_CPP): $(wildcard $(SRC_DIR)/common/*.h)

$(filter $(OBJ_DIR)/soc/%.o $(OBJ_DIR)/board/%.o,$(OBJS_CPP)): $(wildcard $(SRC_DIR)/soc/common/*.h $(SRC_DIR)/soc/sti5512/*.h)

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
	rm -rf $(OBJ_DIR) $(TARGET) st20emu-pgo

.PHONY: all release pgo clean test test-st20sim
