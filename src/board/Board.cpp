#include "Board.h"

namespace {
Board active = Board::None;
}

Board activeBoard() {
  return active;
}

void setActiveBoard(Board board) {
  active = board;
}
