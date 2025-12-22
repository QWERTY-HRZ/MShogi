#pragma once

#include <QObject>
#include <QDebug>

enum class GameState {
    Init,
    Playing,
    PromotionCheck,
    End
};

class GameEngine : public QObject {
    Q_OBJECT

public:
    explicit GameEngine(QObject *parent = nullptr);
    void startGame();
    GameState getCurrentState() const;

signals:
    void stateChanged(GameState newState);

private:
    GameState m_currentState;
};