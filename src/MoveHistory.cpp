#include "MoveHistory.h"
#include <sstream>

namespace {
const char* pieceName(PieceType type) {
    switch (type) {
        case PieceType::King: return "王";
        case PieceType::Rook: return "车";
        case PieceType::Bishop: return "相";
        case PieceType::Pawn: return "兵";
        case PieceType::Hou: return "侯";
    }
    return "";
}
}

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

void MoveHistory::clear() {
    m_stack.clear();
    m_redoStack.clear();
}

const std::vector<HistoryNode>& MoveHistory::getHistory() const {
    return m_stack;
}

std::string MoveHistory::generateNotation(const Board& board, const Move& move,
                                          PieceType movedType,
                                          std::optional<PieceType> capturedType) {
    std::stringstream ss;
    ss << (move.player == Player::Sente ? "先" : "后");
    ss << pieceName(movedType) << " - ";

    const char* cnNums[] = {"一", "二", "三", "四", "五", "六"};
    ss << cnNums[move.toX];
    // 内部 Y 轴向下递增，棋谱行号需转换为先手视角的自下而上。
    ss << cnNums[GameConstants::ROWS - move.toY - 1];

    if (move.isDrop) {
        ss << "打入";
    } else if (movedType != PieceType::Pawn) {
        const auto allies = board.findPieces(move.player, movedType);
        if (allies.size() > 1) {
            const int dx = move.toX - move.fromX;
            const int dy = move.toY - move.fromY;

            if (dx == 0 && dy < 0) ss << "正上";
            else if (dx == 0 && dy > 0) ss << "正下";
            else if (dx > 0 && dy == 0) ss << "正右";
            else if (dx < 0 && dy == 0) ss << "正左";
            else if (dx > 0 && dy < 0) ss << "右上";
            else if (dx > 0 && dy > 0) ss << "右下";
            else if (dx < 0 && dy < 0) ss << "左上";
            else if (dx < 0 && dy > 0) ss << "左下";
        }
    }

    if (capturedType.has_value()) {
        // 吃子后缀使用移动前保存的目标类型。
        ss << " - 吃" << pieceName(*capturedType);
    }

    return ss.str();
}
