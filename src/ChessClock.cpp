#include "../include/ChessClock.h"

ChessClock::ChessClock(QObject *parent) : QObject(parent), m_timer(new QTimer(this)) {
    // 1 秒触发一次
    connect(m_timer, &QTimer::timeout, this, &ChessClock::onTick);
}

void ChessClock::start(int totalTime, int increment, Player startingPlayer) {
    m_totalTimeSetting = totalTime;
    m_increment = increment;
    m_senteTime = totalTime;
    m_goteTime = totalTime;
    m_currentPlayer = startingPlayer;
    m_timer->start(1000);
    emit timeUpdated();
}

void ChessClock::stop() { m_timer->stop(); }
void ChessClock::resume() { m_timer->start(1000); }

void ChessClock::switchTurn(Player nextPlayer) {
    m_currentPlayer = nextPlayer;
}

void ChessClock::addIncrement(Player player) {
    if (player == Player::Sente) m_senteTime += m_increment;
    else m_goteTime += m_increment;
    emit timeUpdated();
}

void ChessClock::setTime(int senteT, int goteT, Player currentPlayer) {
    m_senteTime = senteT;
    m_goteTime = goteT;
    m_currentPlayer = currentPlayer;
    emit timeUpdated();
}

void ChessClock::onTick() {
    if (m_currentPlayer == Player::Sente) {
        m_senteTime--;
        if (m_senteTime <= 0) {
            m_timer->stop();
            // 先手超时 判负
            emit timeout(Player::Sente);
        }
    } else {
        m_goteTime--;
        if (m_goteTime <= 0) {
            m_timer->stop();
            // 后手超时 判负
            emit timeout(Player::Gote);
        }
    }
    emit timeUpdated();
}