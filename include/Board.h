#ifndef BOARD_H
#define BOARD_H

#endif // BOARD_H
#pragma once

#include <vector>
#include <memory>
#include "Piece.h"

class Board {
public:
    Board();
    ~Board();

    bool placePiece(int x, int y, std::shared_ptr<Piece> piece);
    std::shared_ptr<Piece> removePiece(int x, int y);
    bool movePiece(int fromX, int fromY, int toX, int toY);

    std::shared_ptr<Piece> getPiece(int x, int y) const;
    bool isInside(int x, int y) const;
    void clear();

    static constexpr int ROWS = 6;
    static constexpr int COLS = 5;

private:
    std::vector<std::vector<std::shared_ptr<Piece>>> m_grid;
};
