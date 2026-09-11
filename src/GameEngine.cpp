#include "GameEngine.h"
#include <QAudioOutput>
#include <QFutureWatcher>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

GameEngine::GameEngine(QObject *parent)
    : QObject(parent),
      m_currentState(GameState::Init),
      m_clock(new ChessClock(this)),
      m_elapsedTimer(new QTimer(this)),
      m_totalSecondsElapsed(0)
{
    connect(m_elapsedTimer, &QTimer::timeout, this, &GameEngine::onElapsedTimerTick);
    connect(m_clock, &ChessClock::timeout, this, &GameEngine::onClockTimeout);

    auto initPlayer = [this](QMediaPlayer*& player, const QString& path) {
        player = new QMediaPlayer(this);
        auto* audioOutput = new QAudioOutput(player);
        player->setAudioOutput(audioOutput);
        player->setSource(QUrl(path));
        audioOutput->setVolume(1.0f);
    };
    initPlayer(m_sndPlace, "qrc:/res/sounds/place.wav");
    initPlayer(m_sndCapture, "qrc:/res/sounds/capture.wav");
    initPlayer(m_sndOpen, "qrc:/res/sounds/open.wav");
    initPlayer(m_sndEnd, "qrc:/res/sounds/fail.wav");
}

void GameEngine::startGame(int totalTime, int increment) {
    // 重开时先停止旧计时并清空状态，避免旧历史泄漏到新对局。
    m_clock->stop();
    m_elapsedTimer->stop();
    m_core.reset();
    ++m_positionGeneration;
    m_history.clear();

    m_currentState = GameState::Playing;
    m_totalSecondsElapsed = 0;
    m_elapsedTimer->start(1000);
    m_clock->start(totalTime, increment, Player::Sente);

    m_sndOpen->setPosition(0);
    m_sndOpen->play();
    emit kingThreatStatusChanged(false, false);
    emit stateChanged(m_currentState);
    QTimer::singleShot(0, this, &GameEngine::requestAgentMoveIfNeeded);
}

void GameEngine::onElapsedTimerTick() {
    ++m_totalSecondsElapsed;
}

void GameEngine::onClockTimeout(Player loser) {
    finishGame(loser == Player::Sente ? 2 : 1, GameEndReason::Timeout);
}

void GameEngine::resign() {
    if (m_currentState != GameState::Playing) return;
    const int result = (getCurrentPlayer() == Player::Sente) ? 2 : 1;
    // 认输属于不可撤销终局，清空历史以禁用悔棋。
    m_history.clear();
    finishGame(result, GameEndReason::Resignation);
}

bool GameEngine::makeMove(const Move& move) {
    if (m_currentState != GameState::Playing) return false;
    // 先通过核心校验坐标与轮次，再读取棋子生成棋谱。
    if (!m_core.isLegalAction(move)) return false;
    const Board& boardBefore = m_core.board();
    const auto movingPiece = move.isDrop ? nullptr
                                         : boardBefore.getPiece(move.fromX, move.fromY);
    if (!move.isDrop && !movingPiece) return false;
    const PieceType movedType = move.isDrop ? move.dropType : movingPiece->getType();
    const auto target = move.isDrop ? nullptr : boardBefore.getPiece(move.toX, move.toY);
    const std::optional<PieceType> capturedType = target
                                                     ? std::optional<PieceType>(target->getType())
                                                     : std::nullopt;
    const std::string notation =
        MoveHistory::generateNotation(boardBefore, move, movedType, capturedType);

    // 行棋前快照用于精确恢复棋钟和两阶段下底状态。
    const int senteTimeBefore = m_clock->getSenteTime();
    const int goteTimeBefore = m_clock->getGoteTime();
    const bool senteFlagBefore = boardBefore.getKingInBaseFlag(Player::Sente);
    const bool goteFlagBefore = boardBefore.getKingInBaseFlag(Player::Gote);

    CoreMoveResult coreResult;
    // 所有盘面变化统一委托给无界面核心，UI 对局与训练不会出现规则分叉。
    if (!m_core.applyAction(move, &coreResult)) return false;
    ++m_positionGeneration;

    m_clock->addIncrement(move.player);
    m_history.push({move, coreResult.capturedType, coreResult.promoted, nullptr, notation,
                    senteTimeBefore, goteTimeBefore, senteFlagBefore, goteFlagBefore});
    m_clock->switchTurn(m_core.currentPlayer());

    if (coreResult.capturedType.has_value()) {
        m_sndCapture->setPosition(0);
        m_sndCapture->play();
    } else {
        m_sndPlace->setPosition(0);
        m_sndPlace->play();
    }

    emit moveExecuted(notation);

    if (coreResult.terminal) {
        GameEndReason reason = GameEndReason::NoLegalAction;
        if (coreResult.endReason == CoreEndReason::KingCaptured) {
            reason = GameEndReason::KingCaptured;
        } else if (coreResult.endReason == CoreEndReason::BaselineEntry) {
            reason = GameEndReason::BaselineEntry;
        } else if (coreResult.endReason == CoreEndReason::RepetitionDraw) {
            reason = GameEndReason::RepetitionDraw;
        }
        finishGame(coreResult.winner, reason);
        return true;
    }

    emit kingThreatStatusChanged(coreResult.senteThreatened,
                                 coreResult.goteThreatened);
    QTimer::singleShot(0, this, &GameEngine::requestAgentMoveIfNeeded);
    return true;
}

void GameEngine::undo() {
    if (!m_history.canUndo()) return;

    const bool wasEnded = m_currentState == GameState::End;
    const auto nodeOpt = m_history.pop();
    if (!nodeOpt) return;
    HistoryNode restoreNode = *nodeOpt;
    if (!m_core.undoAction()) return;
    ++m_positionGeneration;

    // 人机对局默认撤销一对着法，确保操作后回到人类决策点。
    if (isAgentTurn() && m_history.canUndo()) {
        const auto previousNode = m_history.pop();
        if (previousNode && m_core.undoAction()) {
            restoreNode = *previousNode;
            ++m_positionGeneration;
        }
    }

    // 核心恢复盘面，Qt 层只恢复撤销范围开始前的棋钟。
    m_clock->setTime(restoreNode.senteTimeBefore,
                     restoreNode.goteTimeBefore,
                     restoreNode.move.player);

    if (wasEnded) {
        // 自然终局允许悔棋时，需要同步恢复棋钟和总用时计时器。
        m_currentState = GameState::Playing;
        m_clock->resume();
        m_elapsedTimer->start(1000);
        emit stateChanged(m_currentState);
    }

    // 悔棋恢复盘面后重新计算提示，避免沿用被撤销着法的将军状态。
    emit kingThreatStatusChanged(
        m_core.isKingThreatened(Player::Sente),
        m_core.isKingThreatened(Player::Gote));
    emit undoExecuted();
    QTimer::singleShot(0, this, &GameEngine::requestAgentMoveIfNeeded);
}

void GameEngine::finishGame(int result, GameEndReason reason) {
    if (m_currentState == GameState::End) return;
    m_currentState = GameState::End;
    m_clock->stop();
    m_elapsedTimer->stop();
    ++m_positionGeneration;

    m_sndEnd->setPosition(0);
    m_sndEnd->play();
    emit stateChanged(m_currentState);
    emit gameEnded(result, reason);
}

void GameEngine::pauseGame() {
    if (m_currentState != GameState::Playing) return;
    m_currentState = GameState::Paused;
    m_clock->stop();
    m_elapsedTimer->stop();
    ++m_positionGeneration;
    emit stateChanged(m_currentState);
}

void GameEngine::resumeGame() {
    if (m_currentState != GameState::Paused) return;
    m_currentState = GameState::Playing;
    m_clock->resume();
    m_elapsedTimer->start(1000);
    emit stateChanged(m_currentState);
    QTimer::singleShot(0, this, &GameEngine::requestAgentMoveIfNeeded);
}

void GameEngine::setAgent(Player player, std::shared_ptr<GameAgent> agent) {
    if (player == Player::Sente) m_senteAgent = std::move(agent);
    else m_goteAgent = std::move(agent);
    ++m_positionGeneration;
}

void GameEngine::clearAgents() {
    m_senteAgent.reset();
    m_goteAgent.reset();
    ++m_positionGeneration;
}

bool GameEngine::isAgentTurn() const {
    const auto& agent = getCurrentPlayer() == Player::Sente ? m_senteAgent : m_goteAgent;
    return static_cast<bool>(agent);
}

void GameEngine::requestAgentMoveIfNeeded() {
    if (m_currentState != GameState::Playing || !isAgentTurn()) return;

    const Player agentPlayer = getCurrentPlayer();
    const auto agent = agentPlayer == Player::Sente ? m_senteAgent : m_goteAgent;
    const quint64 generation = m_positionGeneration;
    GameCore snapshot = m_core.fork();
    auto* watcher = new QFutureWatcher<std::optional<Move>>(this);
    connect(watcher, &QFutureWatcher<std::optional<Move>>::finished, this,
            [this, watcher, generation, agentPlayer] {
        const auto selected = watcher->result();
        watcher->deleteLater();
        if (generation != m_positionGeneration ||
            m_currentState != GameState::Playing ||
            getCurrentPlayer() != agentPlayer || !selected) {
            return;
        }
        makeMove(*selected);
    });
    watcher->setFuture(QtConcurrent::run([agent, snapshot = std::move(snapshot)] {
        return agent->chooseAction(snapshot);
    }));
}

std::vector<Move> GameEngine::getLegalMoves(int x, int y) {
    if (m_currentState != GameState::Playing) return {};
    return m_core.legalMovesFrom(x, y);
}

std::vector<Move> GameEngine::getLegalDrops(PieceType type) {
    if (m_currentState != GameState::Playing) return {};
    return m_core.legalDrops(type);
}

GameState GameEngine::getCurrentState() const { return m_currentState; }
const Board& GameEngine::getBoard() const { return m_core.board(); }
const MoveHistory& GameEngine::getHistory() const { return m_history; }

Player GameEngine::getCurrentPlayer() const {
    return m_core.currentPlayer();
}
