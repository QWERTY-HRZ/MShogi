#pragma once
#include <QMainWindow>
#include <QGraphicsView>
#include <QLabel>
#include <QTextEdit>
#include <QTimer>
#include <QPushButton>
#include "GameEngine.h"
#include "GameScene.h"

class UIController : public QMainWindow {
    Q_OBJECT
public:
    explicit UIController(QWidget *parent = nullptr);

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onStateChanged(GameState newState);
    void onMoveExecuted(const std::string& notation);
    void onGameEnded(int result);
    void updateTimer();
    // 悔棋槽函数
    void onUndoExecuted();
    // 重新开始按钮
    void onRestartClicked();
    // 认输按钮
    void onResignClicked();

private:
    void setupUi();
    // 核心功能：处理来自 View 层的移动请求
    bool handleMoveRequest(const Move& move);

    GameEngine* m_gameEngine;
    GameScene* m_scene;
    QGraphicsView* m_view;

    QLabel* m_lblStatus;
    QLabel* m_lblTimer;
    QTextEdit* m_txtHistory;
    QTimer* m_timer;
    int m_secondsElapsed;

    // 记录当前执子方
    Player m_currentPlayer;
};
