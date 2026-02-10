#pragma once

#include <QObject>
#include <memory>
#include "Board.h"
#include "RuleEngine.h"
#include "MoveHistory.h"

enum class GameState {
    Init,
    Playing,
    End
};

class GameEngine : public QObject {
    Q_OBJECT

public:
    explicit GameEngine(QObject *parent = nullptr);

    // 初始化并开始游戏，布局棋子
    void startGame();
    // 执行移动：包含校验、吃子、升变、胜负判定
    bool makeMove(const Move& move);
    // 悔棋：恢复盘面和手驹状态
    void undo();
    // 结束游戏
    void finishGame(int result);

    GameState getCurrentState() const;
    const Board& getBoard() const;
    const MoveHistory& getHistory() const;

signals:
    void stateChanged(GameState newState);
    void gameEnded(int result);
    // 移动执行成功后发送记谱字符串
    void moveExecuted(const std::string& notation);
    // 悔棋完成信号
    void undoExecuted();

private:
    GameState m_currentState;
    Board m_board;
    RuleEngine m_ruleEngine;
    MoveHistory m_history;

    // 辅助工厂方法：创建棋子实例
    std::shared_ptr<Piece> createPiece(PieceType type, Player owner);
};
