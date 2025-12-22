#pragma once

#include <QMainWindow>
#include "GameEngine.h"

class UIController : public QMainWindow {
    Q_OBJECT

public:
    explicit UIController(QWidget *parent = nullptr);

public slots:
    void onStateChanged(GameState newState);

private:
    GameEngine* m_gameEngine;
};