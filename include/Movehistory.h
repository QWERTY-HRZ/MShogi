#pragma once

#include <vector>
#include <string>
#include <optional>
#include "RuleEngine.h"

struct HistoryNode {
    Move move;
    // 被吃掉的棋子类型，用于撤销
    std::optional<PieceType> capturedType;
    // 是否发生了升变，用于侯被吃掉时的撤销
    bool isPromoted;
    // 记谱文本
    std::string notation;
    // 双方剩余时间
    int senteTimeLeft;
    int goteTimeLeft;
};

class MoveHistory {
public:
    void push(const HistoryNode& node);
    std::optional<HistoryNode> pop();
    std::optional<HistoryNode> peek() const;

    bool canUndo() const;
    bool canRedo() const; // 暂不实现重做分支，通常redo需要另一个栈

    // 辅助：生成棋谱
    static std::string generateNotation(const Board& board, const Move& move, const RuleEngine& ruleEngine);

    const std::vector<HistoryNode>& getHistory() const;

private:
    std::vector<HistoryNode> m_stack;
    // 支持 Redo
    std::vector<HistoryNode> m_redoStack;
};
