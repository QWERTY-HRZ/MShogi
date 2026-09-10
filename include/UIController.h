#pragma once
#include <QMainWindow>
#include <QGraphicsView>
#include <QLabel>
#include <QTextEdit>
#include <QTimer>
#include <QPushButton>
#include <QString>
#include "GameEngine.h"
#include "GameScene.h"

class UIController : public QMainWindow {
    Q_OBJECT
public:
    explicit UIController(QWidget *parent = nullptr, bool promptOnStart = true);

protected:
    void resizeEvent(QResizeEvent* event) override;
    // 绘图事件
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onStateChanged(GameState newState);
    void onMoveExecuted(const std::string& notation);
    void onGameEnded(int result, GameEndReason reason);
    void onKingThreatStatusChanged(bool senteThreatened, bool goteThreatened);

    // 统一更新时间
    void onUpdateTimer();
    // 悔棋
    void onUndoExecuted();
    // 重新开始
    void onRestartClicked();
    // 认输
    void onResignClicked();
    // 暂停/继续
    void onPauseResumeClicked();

private:
    void setupUi();
    void scheduleBoardRefresh();
    void refreshHistory();
    // 处理移动请求
    bool handleMoveRequest(const Move& move);
    //开局设置
    bool promptSettingsAndStart();

    GameEngine* m_gameEngine;
    GameScene* m_scene;
    QGraphicsView* m_view;

    QLabel* m_lblStatus;
    QLabel* m_lblOpeningDraw;
    QLabel* m_lblGameInfo;
    QLabel* m_lblSenteTurn;
    QLabel* m_lblGoteTurn;

    QTextEdit* m_txtHistory;
    // 按钮指针
    QPushButton* m_btnPauseResume;
    QPushButton* m_btnUndo;
    QPushButton* m_btnRestart;
    QPushButton* m_btnResign;
    // 背景图片
    QPixmap m_bgPixmap;

    QString m_sentePlayerName = "玩家一";
    QString m_gotePlayerName = "玩家二";
    QString m_checkNotice;

    // 不在 UI 中计时
    // QTimer* m_uiTimer;
    // int m_secondsElapsed;

    // 不再记录当前玩家 而是从 gameEngine 直接获取
    // Player m_currentPlayer;
};
