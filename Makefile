TARGET = st20emu
OBJ_DIR = obj

CC = clang
CXX = clang++

SRCS_CPP = st20emu.cpp omr.cpp stdafx.cpp
SRCS_C   = commands.c memory.c st20.c
OBJS = $(addprefix $(OBJ_DIR)/, $(SRCS_CPP:.cpp=.o) $(SRCS_C:.c=.o))

CFLAGS_BASE   = -std=c23
CXXFLAGS_BASE = -std=c++23 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wold-style-cast
LDFLAGS_BASE  = -lncurses

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

$(OBJ_DIR)/%.o: %.cpp | $(OBJ_DIR)
	@echo "Compiling C++ file $<..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: %.c | $(OBJ_DIR)
	@echo "Compiling C file $<..."
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	@echo "Cleaning up build artifacts..."
	rm -rf $(OBJ_DIR) $(TARGET)

.PHONY: all clean
