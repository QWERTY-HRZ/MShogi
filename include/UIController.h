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

    // 统一更新时间、改变先后手
    void onUpdateTimer();
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

    QLabel* m_lblStatus;      // 第一行: 对局状态
    QLabel* m_lblGameInfo;    // 第二行: 总用时与设置
    QLabel* m_lblSenteTurn;   // 第三行: 先手时间
    QLabel* m_lblGoteTurn;    // 第四行: 后手时间
    // [删除] QLabel* m_lblTimer;


    QTextEdit* m_txtHistory;
    QTimer* m_uiTimer;
    int m_secondsElapsed;

    // 不再记录当前玩家 而是从 gameEngine 直接获取
    // Player m_currentPlayer;
};
