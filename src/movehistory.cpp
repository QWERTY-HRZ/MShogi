#include "../include/MoveHistory.h"
#include <sstream>

void MoveHistory::push(const HistoryNode& node) {
    m_stack.push_back(node);
    m_redoStack.clear();
}

std::optional<HistoryNode> MoveHistory::pop() {
    if (m_stack.empty()) return std::nullopt;
    HistoryNode node = m_stack.back();
    m_stack.pop_back();
    m_redoStack.push_back(node);
    return node;
}

std::optional<HistoryNode> MoveHistory::peek() const {
    if (m_stack.empty()) return std::nullopt;
    return m_stack.back();
}

bool MoveHistory::canUndo() const {
    return !m_stack.empty();
}

bool MoveHistory::canRedo() const {
    return !m_redoStack.empty();
}

const std::vector<HistoryNode>& MoveHistory::getHistory() const {
    return m_stack;
}

std::string MoveHistory::generateNotation(const Board& board, const Move& move, const RuleEngine& ruleEngine) {
    std::stringstream ss;

    // 1. 先后手
    ss << (move.player == Player::Sente ? "先" : "后");

    std::string pieceName;
    // 棋子名称 默认为兵！
    PieceType type = PieceType::Pawn;

    if (move.isDrop) {
        type = move.dropType;
    } else {
        // 移动后，必须从 to 处获取棋子信息！
        auto p = board.getPiece(move.toX, move.toY);
        if (p) type = p->getType();
    }

    switch (type) {
        case PieceType::King: pieceName = "王"; break;
        case PieceType::Rook: pieceName = "车"; break;
        case PieceType::Bishop: pieceName = "相"; break;
        case PieceType::Pawn: pieceName = "兵"; break;
        case PieceType::Hou: pieceName = "侯"; break;
    }
    ss << pieceName << " - ";

    // 3. 坐标 (中文数字)
    const char* cnNums[] = {"一", "二", "三", "四", "五", "六"};
    if (move.toX >= 0 && move.toX < 6) ss << cnNums[move.toX];
    if (move.toY >= 0 && move.toY < 6) ss << cnNums[move.toY];

    // 4. 后缀处理：打入标记 或 方向消歧
    if (move.isDrop) {
        ss << "打入";
    } else if (type != PieceType::Pawn) {
        // 只要棋盘上己方该类棋子数量 > 1，就标记方向
        // findPieces 返回包括自己在内的所有同类棋子
        auto allies = board.findPieces(move.player, type);

        if (allies.size() > 1) {
            int dx = move.toX - move.fromX;
            int dy = move.toY - move.fromY;
            std::string dir;

            // 简单的方向判定
            if (dx == 0 && dy > 0) dir = "正上";
            else if (dx == 0 && dy < 0) dir = "正下";
            else if (dx > 0 && dy == 0) dir = "正右";
            else if (dx < 0 && dy == 0) dir = "正左";
            else if (dx > 0 && dy > 0) dir = "右上";
            else if (dx > 0 && dy < 0) dir = "右下";
            else if (dx < 0 && dy > 0) dir = "左上";
            else if (dx < 0 && dy < 0) dir = "左下";

            ss << dir;
        }
    }

    return ss.str();
}
