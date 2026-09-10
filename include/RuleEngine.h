#pragma once

#include "Board.h"
#include "GameConstants.h"
#include <optional>

struct Move {
    int fromX;
    int fromY;
    int toX;
    int toY;
    Player player;
    bool isDrop;
    PieceType dropType;

    // 行子
    static Move makeMove(int fx, int fy, int tx, int ty, Player p) {
        return {fx, fy, tx, ty, p, false, PieceType::Pawn};
    }

    // 打入
    static Move makeDrop(int tx, int ty, PieceType type, Player p) {
        return {-1, -1, tx, ty, p, true, type};
    }
};

class RuleEngine {
public:
    RuleEngine() = default;

    // 行子是否合法 移除 forbidden 参数
    bool validateMove(const Board& board, const Move& move) const;
    // 判定升变
    bool checkPromotion(const Board& board, const Move& move) const;

    // 同时检查盘面行子和已解禁手驹，判断该方是否还有合法着法。
    bool hasAnyLegalAction(const Board& board, Player player) const;
    // 仅检测王是否正被攻击，不限制被将军方的下一步选择。
    bool isKingThreatened(const Board& board, Player kingOwner) const;

    // 判定游戏是否结束
    int isGameOver(Board& board) const;

private:
    bool validateNormalMove(const Board& board, const Move& move) const;
    // 打入是否合法
    bool validateDrop(const Board& board, const Move& move) const;
    bool canKingMove(int dx, int dy) const;
    bool canRookMove(const Board& board, const Move& move, int dx, int dy) const;
    bool canBishopMove(const Board& board, const Move& move, int dx, int dy) const;
    bool canPawnMove(int dx, int dy, Player p) const;
    bool canHouMove(int dx, int dy, Player p) const;
};
