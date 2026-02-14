#include "../include/Board.h"

Board::Board()
    // 记录先后手的底线 分别为 5/0
    : m_senteBottomLine(GameConstants::SENTE_BASE_Y),
      m_goteBottomLine(GameConstants::GOTE_BASE_Y),
      m_senteFlag(false),
      m_goteFlag(false) {
    // 初始化棋盘
    m_grid.resize(COLS, std::vector<std::shared_ptr<Piece>>(ROWS, nullptr));
}

Board::~Board() { clear(); }

void Board::clear() {
    for (auto& col : m_grid) std::fill(col.begin(), col.end(), nullptr);
    m_hands.clear();
    m_senteFlag = false;
    m_goteFlag = false;
}

bool Board::isInside(int x, int y) const {
    return x >= 0 && x < COLS && y >= 0 && y < ROWS;
}

std::shared_ptr<Piece> Board::getPiece(int x, int y) const {
    if (!isInside(x, y)) return nullptr;
    return m_grid[x][y];
}

std::vector<std::pair<int, int>> Board::findPieces(Player p, PieceType type) const {
    std::vector<std::pair<int, int>> positions;
    for (int x = 0; x < GameConstants::COLS; ++x) {
        for (int y = 0; y < GameConstants::ROWS; ++y) {
            auto piece = m_grid[x][y];
            if (piece && piece->getOwner() == p && piece->getType() == type) {
                positions.push_back({x, y});
            }
        }
    }
    return positions;
}

bool Board::placePiece(int x, int y, std::shared_ptr<Piece> piece) {
    if (!isInside(x, y)) return false;
    m_grid[x][y] = piece;
    return true;
}

std::shared_ptr<Piece> Board::removePiece(int x, int y) {
    if (!isInside(x, y)) return nullptr;
    auto piece = m_grid[x][y];
    m_grid[x][y] = nullptr;
    return piece;
}

bool Board::movePiece(int fromX, int fromY, int toX, int toY) {
    if (!isInside(fromX, fromY) || !isInside(toX, toY)) return false;
    auto piece = m_grid[fromX][fromY];

    // 防止后续代码先赋值再置空 导致棋子丢失
    if (fromX == toX && fromY == toY) return false;
    if (!piece) return false;
    m_grid[toX][toY] = piece;
    m_grid[fromX][fromY] = nullptr;
    return true;
}

int Board::getBottomLine(Player p) const {
    // 获取【对方】的底线
    return (p == Player::Sente) ? GameConstants::GOTE_BASE_Y : GameConstants::SENTE_BASE_Y;
}

bool Board::getKingInBaseFlag(Player p) const {
    return (p == Player::Sente) ? m_senteFlag : m_goteFlag;
}

void Board::setKingInBaseFlag(Player p, bool val) {
    if (p == Player::Sente) m_senteFlag = val;
    else m_goteFlag = val;
}

void Board::addToHand(Player p, PieceType type) {
    m_hands[p][type]++;
}

bool Board::removeFromHand(Player p, PieceType type) {
    if (m_hands[p][type] > 0) {
        m_hands[p][type]--;
        return true;
    }
    return false;
}

int Board::getHandCount(Player p, PieceType type) const {
    auto it = m_hands.find(p);
    if (it != m_hands.end()) {
        auto typeIt = it->second.find(type);
        if (typeIt != it->second.end()) {
            return typeIt->second;
        }
    }
    return 0;
}
