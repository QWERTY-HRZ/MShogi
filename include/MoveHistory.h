#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "RuleEngine.h"

struct HistoryNode {
    Move move;
    // 保存被吃棋子的盘面类型，以及进入手驹区后的具体对象。
    std::optional<PieceType> capturedType;
    bool isPromoted;
    std::shared_ptr<Piece> capturedHandPiece;
    std::string notation;
    // 保存行棋前快照，悔棋时恢复棋钟和下底判定。
    int senteTimeBefore;
    int goteTimeBefore;
    bool senteKingInBaseBefore;
    bool goteKingInBaseBefore;
};

class MoveHistory {
public:
    void push(const HistoryNode& node);
    std::optional<HistoryNode> pop();
    std::optional<HistoryNode> peek() const;

    bool canUndo() const;
    bool canRedo() const;
    void clear();

    static std::string generateNotation(const Board& board, const Move& move,
                                        PieceType movedType,
                                        std::optional<PieceType> capturedType = std::nullopt);

    const std::vector<HistoryNode>& getHistory() const;

private:
    std::vector<HistoryNode> m_stack;
    std::vector<HistoryNode> m_redoStack;
};
