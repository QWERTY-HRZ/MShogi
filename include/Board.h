#pragma once

#include <vector>
#include <memory>
#include <map>
#include "Piece.h"
#include "GameConstants.h"

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

    // 手驹区 添加/去除
    void addToHand(std::shared_ptr<Piece> piece);
    bool removeFromHand(Player p, PieceType type);
    // 返回手驹区：改为指针列表
    const std::vector<std::shared_ptr<Piece>>& getHand(Player p) const;
    // 新增：更新回合数
    void updateHandTurns(int delta);

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
};
