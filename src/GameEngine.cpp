#include "GameEngine.h"

GameEngine::GameEngine(QObject *parent) 
    : QObject(parent), m_currentState(GameState::Init) {
}

void GameEngine::startGame() {
    m_currentState = GameState::Playing;
    emit stateChanged(m_currentState);
}

GameState GameEngine::getCurrentState() const {
    return m_currentState;
}