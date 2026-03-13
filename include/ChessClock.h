#pragma once
#include <QObject>
#include <QTimer>
#include "Piece.h"

class ChessClock : public QObject {
    Q_OBJECT
public:
    explicit ChessClock(QObject *parent = nullptr);

    // 初始化并启动棋钟
    void start(int totalTime, int increment, Player startingPlayer = Player::Sente);
    void stop();
    void resume();
    
    // 切换棋钟回合
    void switchTurn(Player nextPlayer);
    // 为指定玩家增加奖励时间
    void addIncrement(Player player);
    // 强制设置时间，用于悔棋回退
    void setTime(int senteT, int goteT, Player currentPlayer);
    // 先后手时间、总时长、每步奖励
    int getSenteTime() const { return m_senteTime; }
    int getGoteTime() const { return m_goteTime; }
    int getTotalSetting() const { return m_totalTimeSetting; }
    int getIncrement() const { return m_increment; }

signals:
    void timeUpdated();         // 每秒刷新 UI 信号
    void timeout(Player loser); // 超时信号，传递超时超时信号

private slots:
    void onTick();

private:
    QTimer* m_timer;
    int m_senteTime;
    int m_goteTime;
    int m_totalTimeSetting;
    int m_increment;
    Player m_currentPlayer;
};