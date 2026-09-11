#pragma once

#include <vector>
#include <memory>
#include <map>
#include "Piece.h"
#include "GameConstants.h"

class Board {
public:
    Board();
    Board(const Board& other);
    Board& operator=(const Board& other);
    Board(Board&&) noexcept = default;
    Board& operator=(Board&&) noexcept = default;
    ~Board();

    bool placePiece(int x, int y, std::shared_ptr<Piece> piece);
    std::shared_ptr<Piece> removePiece(int x, int y);
    bool movePiece(int fromX, int fromY, int toX, int toY);

    std::shared_ptr<Piece> getPiece(int x, int y) const;
    std::vector<std::pair<int, int>> findPieces(Player p, PieceType type) const;

    bool isInside(int x, int y) const;
    void clear();

    int getBottomLine(Player p) const;
    bool getKingInBaseFlag(Player p) const;
    void setKingInBaseFlag(Player p, bool val);

    // 手驹按具体对象管理，避免同类棋子的禁手状态互相混淆。
    void addToHand(std::shared_ptr<Piece> piece);
    std::shared_ptr<Piece> takeFromHand(Player p, PieceType type);
    bool removeFromHand(const std::shared_ptr<Piece>& piece);
    bool hasDroppablePiece(Player p, PieceType type) const;
    const std::vector<std::shared_ptr<Piece>>& getHand(Player p) const;
    void advanceHandTurns();

    static constexpr int ROWS = GameConstants::ROWS;
    static constexpr int COLS = GameConstants::COLS;
private:
    std::vector<std::vector<std::shared_ptr<Piece>>> m_grid;
    // 储存具体棋子对象
    std::map<Player, std::vector<std::shared_ptr<Piece>>> m_hands;

    // 记录底线数据
    int m_senteBottomLine;
    int m_goteBottomLine;

    // 记录是否下底
    bool m_senteFlag;
    bool m_goteFlag;

    static bool isDroppable(const std::shared_ptr<Piece>& piece);
};
