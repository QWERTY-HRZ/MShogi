#include "../include/Board.h"

Board::Board()
    : m_senteBottomLine(5),
      m_goteBottomLine(0),
      m_senteFlag(false),
      m_goteFlag(false)
{
    // 初始化棋盘
    m_grid.resize(COLS, std::vector<std::shared_ptr<Piece>>(ROWS, nullptr));
}

Board::~Board() {
    clear();
}

void Board::clear() {
    for (auto& col : m_grid) {
        std::fill(col.begin(), col.end(), nullptr);
    }
    m_senteFlag = false;
    m_goteFlag = false;
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
    // 吃子
    if (!isInside(x, y)) {
        return nullptr;
    }
    auto piece = m_grid[x][y];
    m_grid[x][y] = nullptr;
    // 返回被吃的子
    return piece;
}

bool Board::movePiece(int fromX, int fromY, int toX, int toY) {
    // 坐标是否合法
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

    // 棋子行动
    m_grid[toX][toY] = piece;
    m_grid[fromX][fromY] = nullptr;
    return true;
}

int Board::getBottomLine(Player p) const {
    return (p == Player::Sente) ? m_senteBottomLine : m_goteBottomLine;
}

bool Board::getKingInBaseFlag(Player p) const {
    // 每回合判断王是否下底 用于计算胜利条件
    return (p == Player::Sente) ? m_senteFlag : m_goteFlag;
}

void Board::setKingInBaseFlag(Player p, bool val) {
    if (p == Player::Sente) {
        m_senteFlag = val;
    } else {
        m_goteFlag = val;
    }
}
