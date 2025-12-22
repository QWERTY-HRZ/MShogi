#include "UIController.h"
#include <QLabel>

UIController::UIController(QWidget *parent) : QMainWindow(parent) {
    m_gameEngine = new GameEngine(this);
    
    connect(m_gameEngine, &GameEngine::stateChanged, 
            this, &UIController::onStateChanged);
            
    resize(1024, 768);
    m_gameEngine->startGame();
}

void UIController::onStateChanged(GameState newState) {
    if (newState == GameState::Playing) {
        setWindowTitle("MShogi - Playing");
    }
}