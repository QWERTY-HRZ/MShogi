#pragma once

#include <QObject>
#include <QTimer>
#include <memory>
#include "Board.h"
#include "RuleEngine.h"
#include "MoveHistory.h"
#include "ChessClock.h"

enum class GameState {
    // 状态：初始化、游戏中、暂停、游戏结束
    Init,
    Playing,
    Paused,
    End
};

class GameEngine : public QObject {
    Q_OBJECT

public:
    explicit GameEngine(QObject *parent = nullptr);

    // 初始化并开始游戏 设置棋钟时间
    void startGame(int totalTime, int increment);
    // 执行移动：包含校验、吃子、升变、胜负判定
    bool makeMove(const Move& move);
    // 悔棋：恢复盘面和手驹状态
    void undo();
    // 处理投降信息
    void resign();
    // 结束游戏
    void finishGame(int result);
    // 暂停与继续
    void pauseGame();
    void resumeGame();

    // 查询可走位置
    std::vector<Move> getLegalMoves(int x, int y);
    // 查询可打入位置
    std::vector<Move> getLegalDrops(PieceType type);
    // Getters
    ChessClock* getClock() const { return m_clock; }
    int getTotalSecondsElapsed() const { return m_totalSecondsElapsed; }
    GameState getCurrentState() const;
    Player getCurrentPlayer() const;
    const Board& getBoard() const;
    const MoveHistory& getHistory() const;

signals:
    void stateChanged(GameState newState);
    void gameEnded(int result);
    // 移动执行成功
    void moveExecuted(const std::string& notation);
    // 悔棋完成
    void undoExecuted();

private slots:
    // 响应棋钟超时
    void onClockTimeout(Player loser);
    // 总耗时信号槽
    void onElapsedTimerTick();

private:
    GameState m_currentState;
    Board m_board;
    RuleEngine m_ruleEngine;
    MoveHistory m_history;
    // 棋钟
    ChessClock* m_clock;
    // 总耗时数据
    QTimer* m_elapsedTimer;
    int m_totalSecondsElapsed;
    // 创建棋子实例
    std::shared_ptr<Piece> createPiece(PieceType type, Player owner);
};
