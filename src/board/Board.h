#ifndef BOARD_H
#define BOARD_H

/* the board parts outside the SoC that were initialized (--board) */
enum class Board { None, Dsr9500, Stb6300 };

Board activeBoard();
void setActiveBoard(Board board);

#endif
