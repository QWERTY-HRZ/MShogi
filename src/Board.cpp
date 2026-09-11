#include "Board.h"
#include <algorithm>

Board::Board()
    // 记录先后手的底线 分别为 5/0
    : m_senteBottomLine(GameConstants::SENTE_BASE_Y),
      m_goteBottomLine(GameConstants::GOTE_BASE_Y),
      m_senteFlag(false),
      m_goteFlag(false) {
    // 初始化棋盘
    m_grid.resize(COLS, std::vector<std::shared_ptr<Piece>>(ROWS, nullptr));
}

Board::Board(const Board& other) : Board() {
    *this = other;
}

Board& Board::operator=(const Board& other) {
    if (this == &other) return *this;

    clear();
    m_senteBottomLine = other.m_senteBottomLine;
    m_goteBottomLine = other.m_goteBottomLine;
    m_senteFlag = other.m_senteFlag;
    m_goteFlag = other.m_goteFlag;

    // 搜索分支必须复制棋子对象，不能共享会变化的手驹禁手计数。
    for (int x = 0; x < COLS; ++x) {
        for (int y = 0; y < ROWS; ++y) {
            const auto source = other.getPiece(x, y);
            if (!source) continue;
            auto copy = makePiece(source->getType(), source->getOwner());
            copy->setTurnsInHand(source->getTurnsInHand());
            m_grid[x][y] = copy;
        }
    }
    for (const Player player : {Player::Sente, Player::Gote}) {
        for (const auto& source : other.getHand(player)) {
            auto copy = makePiece(source->getType(), source->getOwner());
            copy->setTurnsInHand(source->getTurnsInHand());
            m_hands[player].push_back(copy);
        }
    }
    return *this;
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
    // 普通摆放只接受空格；吃子覆盖统一由 movePiece 处理。
    if (!isInside(x, y) || !piece || m_grid[x][y]) return false;
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
    return (p == Player::Sente) ? m_goteBottomLine : m_senteBottomLine;
}

bool Board::getKingInBaseFlag(Player p) const {
    return (p == Player::Sente) ? m_senteFlag : m_goteFlag;
}

void Board::setKingInBaseFlag(Player p, bool val) {
    if (p == Player::Sente) m_senteFlag = val;
    else m_goteFlag = val;
}

void Board::addToHand(std::shared_ptr<Piece> piece) {
    if (!piece) return;
    // 默认棋子进入手驹区时规范为已解禁；吃子生成的 1 状态保持不变。
    if (piece->getTurnsInHand() == 0) {
        piece->setTurnsInHand(Piece::HAND_READY_TURNS);
    }
    m_hands[piece->getOwner()].push_back(piece);
}

bool Board::isDroppable(const std::shared_ptr<Piece>& piece) {
    if (!piece) return false;
    const int turns = piece->getTurnsInHand();
    // 1~3 为禁手期，4 是唯一的已解禁手驹状态。
    return turns == Piece::HAND_READY_TURNS;
}

std::shared_ptr<Piece> Board::takeFromHand(Player p, PieceType type) {
    // 只取已解禁的具体对象，不再按类型强制移除禁手棋子。
    auto& list = m_hands[p];
    for (auto it = list.begin(); it != list.end(); ++it) {
        if ((*it)->getType() == type && isDroppable(*it)) {
            auto piece = *it;
            list.erase(it);
            return piece;
        }
    }
    return nullptr;
}

bool Board::removeFromHand(const std::shared_ptr<Piece>& piece) {
    // 按对象地址删除，确保悔棋不会误删另一个同类手驹。
    if (!piece) return false;
    auto& list = m_hands[piece->getOwner()];
    const auto it = std::find(list.begin(), list.end(), piece);
    if (it == list.end()) return false;
    list.erase(it);
    return true;
}

bool Board::hasDroppablePiece(Player p, PieceType type) const {
    const auto it = m_hands.find(p);
    if (it == m_hands.end()) return false;
    return std::any_of(it->second.begin(), it->second.end(), [type](const auto& piece) {
        return piece->getType() == type && isDroppable(piece);
    });
}

const std::vector<std::shared_ptr<Piece>>& Board::getHand(Player p) const {
    // 获取手驹区棋子 不止是数量
    static const std::vector<std::shared_ptr<Piece>> empty;
    auto it = m_hands.find(p);
    return (it != m_hands.end()) ? it->second : empty;
}

void Board::advanceHandTurns() {
    // 每步只推进到解禁态，悔棋由 GameCore 快照精确恢复。
    for (auto& kv : m_hands) {
        for (std::shared_ptr<Piece> p : kv.second) {
            p->advanceTurnInHand();
        }
    }
}
