#pragma once

#include <QObject>
#include <QDebug>
#include "Board.h"
#include "RuleEngine.h"
#include "MoveHistory.h"

enum class GameState {
    Init,
    Playing,
    PromotionCheck, // 架构要求 保留检查升变的状态
    End
};

class GameEngine : public QObject {
    Q_OBJECT

public:
    explicit GameEngine(QObject *parent = nullptr);

    void startGame();
    bool makeMove(const Move& move);
    void undo();
    void redo(); // 预留接口
    void finishGame(int result); // result: 1=SenteWin, 2=GoteWin

    GameState getCurrentState() const;
    const Board& getBoard() const;
    const MoveHistory& getHistory() const;

signals:
    void stateChanged(GameState newState);
    void gameEnded(int result);
    void moveExecuted(const std::string& notation);

private:
    GameState m_currentState;
    Board m_board;
    RuleEngine m_ruleEngine;
    MoveHistory m_history;

    // 辅助创建棋子的对象
    std::shared_ptr<Piece> createPiece(PieceType type, Player owner);
};
