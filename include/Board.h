#pragma once

#include <vector>
#include <memory>
#include <map>
#include "Piece.h"

class Board {
public:
    Board();
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

    // 手中棋子管理
    void addToHand(Player p, PieceType type);
    bool removeFromHand(Player p, PieceType type);
    int getHandCount(Player p, PieceType type) const;

    static constexpr int ROWS = 6;
    static constexpr int COLS = 5;
private:
    std::vector<std::vector<std::shared_ptr<Piece>>> m_grid;
    std::map<Player, std::map<PieceType, int>> m_hands;

    // 记录底线数据
    int m_senteBottomLine;
    int m_goteBottomLine;

    // 记录是否下底
    bool m_senteFlag;
    bool m_goteFlag;
};
