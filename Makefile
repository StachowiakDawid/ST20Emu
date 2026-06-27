TARGET = st20emu

OBJ_DIR = obj

CC = clang
CXX = clang++

# -w is there temporarily because of around 3 thousand warnings xD
CFLAGS = -O2 -std=c89 -w
CXXFLAGS = -O2 -std=c++11
LDFLAGS = -lncurses

SRCS_CPP = st20emu.cpp omr.cpp stdafx.cpp
SRCS_C   = commands.c memory.c st20.c

OBJS = $(addprefix $(OBJ_DIR)/, $(SRCS_CPP:.cpp=.o) $(SRCS_C:.c=.o))

all: $(TARGET)

$(TARGET): $(OBJS)
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
