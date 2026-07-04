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

CFLAGS_BASE   = -std=c23
CXXFLAGS_BASE = -std=c++23 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wold-style-cast
LDFLAGS_BASE  =

BUILD ?= debug

ifeq ($(BUILD), release)
  CFLAGS   = $(CFLAGS_BASE) -O3 -DNDEBUG
  CXXFLAGS = $(CXXFLAGS_BASE) -O3 -DNDEBUG
  LDFLAGS  = $(LDFLAGS_BASE)
  MSG      = "Building in RELEASE mode..."
else
  CFLAGS   = $(CFLAGS_BASE) -O0 -g -fsanitize=address,undefined
  CXXFLAGS = $(CXXFLAGS_BASE) -O0 -g -fsanitize=address,undefined
  LDFLAGS  = $(LDFLAGS_BASE) -fsanitize=address,undefined
  MSG      = "Building in DEBUG mode with sanitizers..."
endif

all: $(TARGET)

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

.PHONY: all clean
