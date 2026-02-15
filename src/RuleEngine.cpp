#include "../include/RuleEngine.h"
#include <cmath>
#include <algorithm>

bool RuleEngine::validateMove(const Board& board, const Move& move) const {
    if (!board.isInside(move.toX, move.toY)) return false;

    if (move.isDrop) {
        // 打入判断 但不再传参
        return validateDrop(board, move);
    } else {
        if (!board.isInside(move.fromX, move.fromY)) return false;
        // 普通移动判断
        return validateNormalMove(board, move);
    }
}

bool RuleEngine::validateDrop(const Board& board, const Move& move) const {
    // 只允许打入空格
    if (board.getPiece(move.toX, move.toY) != nullptr) return false;
    // 不在此处限制打入回合数
    // 侯打入时变回兵
    if (move.dropType == PieceType::Hou) return false;

    int bottomLine = board.getBottomLine(move.player);
    if (move.toY == bottomLine) return false;

    // 规则：车和相只能放在靠近己方的三行
    if (move.dropType == PieceType::Rook || move.dropType == PieceType::Bishop) {
        if (move.player == Player::Sente) {
            if (move.toY < GameConstants::ZONE_HEIGHT) return false;
        } else {
            if (move.toY >= GameConstants::ZONE_HEIGHT) return false;
        }
    }

    return true;
}

bool RuleEngine::validateNormalMove(const Board& board, const Move& move) const {
    auto piece = board.getPiece(move.fromX, move.fromY);
    if (!piece) return false;
    if (piece->getOwner() != move.player) return false;
    // 不能原位移动
    if (move.fromX == move.toX && move.fromY == move.toY) return false;

    auto target = board.getPiece(move.toX, move.toY);
    if (target && target->getOwner() == move.player) return false;

    int dx = move.toX - move.fromX;
    int dy = move.toY - move.fromY;

    switch (piece->getType()) {
        case PieceType::King:   return canKingMove(dx, dy);
        case PieceType::Rook:   return canRookMove(board, move, dx, dy);
        case PieceType::Bishop: return canBishopMove(board, move, dx, dy);
        case PieceType::Pawn:   return canPawnMove(dx, dy, move.player);
        case PieceType::Hou:    return canHouMove(dx, dy, move.player);
        default: return false;
    }
}

bool RuleEngine::canKingMove(int dx, int dy) const {
    // 八向移动一格
    return std::abs(dx) <= 1 && std::abs(dy) <= 1;
}

bool RuleEngine::canRookMove(const Board& board, const Move& move, int dx, int dy) const {
    if (dx != 0 && dy != 0) return false;

    int dist = std::abs(dx + dy);
    if (dist == 1) return true;

    int steps = std::max(std::abs(dx), std::abs(dy));
    int stepX = (dx == 0) ? 0 : (dx > 0 ? 1 : -1);
    int stepY = (dy == 0) ? 0 : (dy > 0 ? 1 : -1);
    // 连续移动时，路径需要为空
    for (int i = 1; i < steps; ++i) {
        if (board.getPiece(move.fromX + i * stepX, move.fromY + i * stepY) != nullptr) {
            return false;
        }
    }

    if (board.getPiece(move.toX, move.toY) != nullptr) {
        return false;
    }

    return true;
}

bool RuleEngine::canBishopMove(const Board& board, const Move& move, int dx, int dy) const {
    if (std::abs(dx) != std::abs(dy)) return false;

    int dist = std::abs(dx);
    if (dist == 1) return true;

    // 斜向一格有己方棋子时，可在连接方向两格处吃子
    if (dist == 2) {
        int midX = move.fromX + dx / 2;
        int midY = move.fromY + dy / 2;
        auto midPiece = board.getPiece(midX, midY);
        auto targetPiece = board.getPiece(move.toX, move.toY);
        // validateNormalMove 已排除己方棋子可能，此处只要 target 存在即可
        if (midPiece && midPiece->getOwner() == move.player) {
            // 只能吃子
            if (targetPiece) {
                return true;
            }
        }
    }

    return false;
}

bool RuleEngine::canPawnMove(int dx, int dy, Player p) const {
    if (dx != 0) return false;
    // 先手在 Y=5，进攻方向是减小Y；后手进攻是增大Y
    return (p == Player::Sente) ? (dy == -1) : (dy == 1);
}

bool RuleEngine::canHouMove(int dx, int dy, Player p) const {
    if (std::abs(dx) > 1 || std::abs(dy) > 1) return false;

    if (p == Player::Sente) {
        // 不能去斜下方
        if (dy == 1 && dx != 0) return false;
    } else {
        if (dy == -1 && dx != 0) return false;
    }

    return true;
}

bool RuleEngine::checkPromotion(const Board& board, const Move& move) const {
    if (move.isDrop) return false;

    auto piece = board.getPiece(move.fromX, move.fromY);
    if (!piece || piece->getType() != PieceType::Pawn) return false;

    int bottomLine = board.getBottomLine(move.player);
    // 最后判断是否走到对方底线
    return move.toY == bottomLine;
}

int RuleEngine::isGameOver(Board& board) const {
    bool senteKingExists = false;
    bool goteKingExists = false;
    bool senteKingAtBottom = false;
    bool goteKingAtBottom = false;
    // 先手为 0，后手为 5
    // int senteBottom = GameConstants::GOTE_BASE_Y;
    // int goteBottom = GameConstants::SENTE_BASE_Y;

    // 将死规则判定
    for (int i = 0; i < GameConstants::COLS; ++i) {
        for (int j = 0; j < GameConstants::ROWS; ++j) {
            auto p = board.getPiece(i, j);
            if (p && p->getType() == PieceType::King) {
                // 下底【王到对方底线】
                if (p->getOwner() == Player::Sente) {
                    senteKingExists = true;
                    if (j == GameConstants::GOTE_BASE_Y) senteKingAtBottom = true;
                } else {
                    goteKingExists = true;
                    if (j == GameConstants::SENTE_BASE_Y) goteKingAtBottom = true;
                }
            }
        }
    }

    if (!senteKingExists) return 2;
    if (!goteKingExists) return 1;

    // 下底规则判定
    // 如果上一回合已到底线，本回合未被吃则获胜
    if (board.getKingInBaseFlag(Player::Sente) && senteKingExists) return 1;
    if (board.getKingInBaseFlag(Player::Gote) && goteKingExists) return 2;

    // 更新 Flag 供下回合使用
    board.setKingInBaseFlag(Player::Sente, senteKingAtBottom);
    board.setKingInBaseFlag(Player::Gote, goteKingAtBottom);

    return 0;
}
