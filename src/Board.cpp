#include "../include/Board.h"

Board::Board() {
    m_grid.resize(COLS, std::vector<std::shared_ptr<Piece>>(ROWS, nullptr));
}

Board::~Board() {
    clear();
}

void Board::clear() {
    for (auto& col : m_grid) {
        std::fill(col.begin(), col.end(), nullptr);
    }
}

bool Board::isInside(int x, int y) const {
    return x >= 0 && x < COLS && y >= 0 && y < ROWS;
}

std::shared_ptr<Piece> Board::getPiece(int x, int y) const {
    if (!isInside(x, y)) {
        return nullptr;
    }
    return m_grid[x][y];
}

bool Board::placePiece(int x, int y, std::shared_ptr<Piece> piece) {
    if (!isInside(x, y)) {
        return false;
    }
    m_grid[x][y] = piece;
    return true;
}

std::shared_ptr<Piece> Board::removePiece(int x, int y) {
    if (!isInside(x, y)) {
        return nullptr;
    }
    auto piece = m_grid[x][y];
    m_grid[x][y] = nullptr;
    return piece;
}

bool Board::movePiece(int fromX, int fromY, int toX, int toY) {
    if (!isInside(fromX, fromY) || !isInside(toX, toY)) {
        return false;
    }

    auto piece = m_grid[fromX][fromY];
    if (!piece) {
        return false;
    }

    if (fromX == toX && fromY == toY) {
        return false;
    }

    m_grid[toX][toY] = piece;
    m_grid[fromX][fromY] = nullptr;
    return true;
}
