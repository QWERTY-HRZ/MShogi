#include "GameEngine.h"
#include <QAudioOutput>

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
    m_board.clear();
    m_history.clear();

    const int maxY = GameConstants::SENTE_BASE_Y;
    const int minY = GameConstants::GOTE_BASE_Y;

    m_board.placePiece(2, maxY, createPiece(PieceType::King, Player::Sente));
    m_board.placePiece(0, maxY, createPiece(PieceType::Bishop, Player::Sente));
    m_board.placePiece(4, maxY, createPiece(PieceType::Rook, Player::Sente));
    m_board.placePiece(0, maxY - 1, createPiece(PieceType::Pawn, Player::Sente));
    m_board.placePiece(2, maxY - 1, createPiece(PieceType::Pawn, Player::Sente));
    m_board.placePiece(4, maxY - 1, createPiece(PieceType::Pawn, Player::Sente));

    m_board.placePiece(2, minY, createPiece(PieceType::King, Player::Gote));
    m_board.placePiece(0, minY, createPiece(PieceType::Rook, Player::Gote));
    m_board.placePiece(4, minY, createPiece(PieceType::Bishop, Player::Gote));
    m_board.placePiece(0, minY + 1, createPiece(PieceType::Pawn, Player::Gote));
    m_board.placePiece(2, minY + 1, createPiece(PieceType::Pawn, Player::Gote));
    m_board.placePiece(4, minY + 1, createPiece(PieceType::Pawn, Player::Gote));

    m_currentState = GameState::Playing;
    m_totalSecondsElapsed = 0;
    m_elapsedTimer->start(1000);
    m_clock->start(totalTime, increment, Player::Sente);

    m_sndOpen->setPosition(0);
    m_sndOpen->play();
    emit kingThreatStatusChanged(false, false);
    emit stateChanged(m_currentState);
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
    // 引擎必须兜底校验轮次，不能只依赖 UI 拦截。
    if (move.player != getCurrentPlayer()) return false;
    if (!m_ruleEngine.validateMove(m_board, move)) return false;

    // 在改变盘面前记录类型，避免升变后棋谱把“兵”误写成“侯”。
    const auto movingPiece = move.isDrop ? nullptr : m_board.getPiece(move.fromX, move.fromY);
    const PieceType movedType = move.isDrop ? move.dropType : movingPiece->getType();
    const auto target = move.isDrop ? nullptr : m_board.getPiece(move.toX, move.toY);
    const std::optional<PieceType> capturedType =
        target ? std::optional<PieceType>(target->getType()) : std::nullopt;
    const bool shouldPromote = !move.isDrop && m_ruleEngine.checkPromotion(m_board, move);
    const std::string notation =
        MoveHistory::generateNotation(m_board, move, movedType, capturedType);

    // 行棋前快照用于精确恢复棋钟和两阶段下底状态。
    const int senteTimeBefore = m_clock->getSenteTime();
    const int goteTimeBefore = m_clock->getGoteTime();
    const bool senteFlagBefore = m_board.getKingInBaseFlag(Player::Sente);
    const bool goteFlagBefore = m_board.getKingInBaseFlag(Player::Gote);

    std::shared_ptr<Piece> capturedHandPiece;
    if (move.isDrop) {
        auto droppedPiece = m_board.takeFromHand(move.player, move.dropType);
        if (!droppedPiece) return false;
        if (!m_board.placePiece(move.toX, move.toY, droppedPiece)) {
            m_board.addToHand(droppedPiece);
            return false;
        }
    } else {
        // 保存本次捕获生成的手驹对象，撤销时按对象而非类型删除。
        if (target && target->getType() != PieceType::King) {
            const PieceType handType =
                target->getType() == PieceType::Hou ? PieceType::Pawn : target->getType();
            capturedHandPiece = createPiece(handType, move.player);
            capturedHandPiece->setTurnsInHand(1);
            m_board.addToHand(capturedHandPiece);
        }

        if (!m_board.movePiece(move.fromX, move.fromY, move.toX, move.toY)) return false;

        if (shouldPromote) {
            m_board.removePiece(move.toX, move.toY);
            m_board.placePiece(move.toX, move.toY, createPiece(PieceType::Hou, move.player));
        }
    }

    // 每完成一步统一推进手驹回合数，保证“下一次己方回合不能打入”。
    m_board.updateHandTurns(1);
    m_clock->addIncrement(move.player);
    m_history.push({move, capturedType, shouldPromote, capturedHandPiece, notation,
                    senteTimeBefore, goteTimeBefore, senteFlagBefore, goteFlagBefore});
    m_clock->switchTurn(move.player == Player::Sente ? Player::Gote : Player::Sente);

    if (capturedType.has_value()) {
        m_sndCapture->setPosition(0);
        m_sndCapture->play();
    } else {
        m_sndPlace->setPosition(0);
        m_sndPlace->play();
    }

    emit moveExecuted(notation);

    const int result = m_ruleEngine.isGameOver(m_board);
    if (result != 0) {
        const GameEndReason reason = capturedType == PieceType::King
                                         ? GameEndReason::KingCaptured
                                         : GameEndReason::BaselineEntry;
        finishGame(result, reason);
        return true;
    }

    const Player nextPlayer = getCurrentPlayer();
    if (!m_ruleEngine.hasAnyLegalAction(m_board, nextPlayer)) {
        // 无合法着法方直接判负，避免对局停在无法继续的状态。
        const int winner = move.player == Player::Sente ? 1 : 2;
        finishGame(winner, GameEndReason::NoLegalAction);
        return true;
    }

    // 同时报告双方王的威胁状态，但不阻止任何一方选择其他合法着法。
    emit kingThreatStatusChanged(
        m_ruleEngine.isKingThreatened(m_board, Player::Sente),
        m_ruleEngine.isKingThreatened(m_board, Player::Gote));
    return true;
}

void GameEngine::undo() {
    if (!m_history.canUndo()) return;

    const bool wasEnded = m_currentState == GameState::End;
    const auto nodeOpt = m_history.pop();
    if (!nodeOpt) return;
    const HistoryNode& node = *nodeOpt;

    // 先回退所有在手驹区对象的禁手回合，再逆向恢复本步差分。
    m_board.updateHandTurns(-1);

    if (node.move.isDrop) {
        auto droppedPiece = m_board.removePiece(node.move.toX, node.move.toY);
        m_board.addToHand(droppedPiece);
    } else {
        auto movedPiece = m_board.removePiece(node.move.toX, node.move.toY);
        if (node.isPromoted) {
            movedPiece = createPiece(PieceType::Pawn, node.move.player);
        }
        m_board.placePiece(node.move.fromX, node.move.fromY, movedPiece);

        if (node.capturedType.has_value()) {
            m_board.removeFromHand(node.capturedHandPiece);
            const Player enemy =
                node.move.player == Player::Sente ? Player::Gote : Player::Sente;
            m_board.placePiece(node.move.toX, node.move.toY,
                               createPiece(*node.capturedType, enemy));
        }
    }

    // 恢复行棋前快照，避免悔棋后立即误判下底胜利或丢失思考时间。
    m_board.setKingInBaseFlag(Player::Sente, node.senteKingInBaseBefore);
    m_board.setKingInBaseFlag(Player::Gote, node.goteKingInBaseBefore);
    m_clock->setTime(node.senteTimeBefore, node.goteTimeBefore, node.move.player);

    if (wasEnded) {
        // 自然终局允许悔棋时，需要同步恢复棋钟和总用时计时器。
        m_currentState = GameState::Playing;
        m_clock->resume();
        m_elapsedTimer->start(1000);
        emit stateChanged(m_currentState);
    }

    // 悔棋恢复盘面后重新计算提示，避免沿用被撤销着法的将军状态。
    emit kingThreatStatusChanged(
        m_ruleEngine.isKingThreatened(m_board, Player::Sente),
        m_ruleEngine.isKingThreatened(m_board, Player::Gote));
    emit undoExecuted();
}

void GameEngine::finishGame(int result, GameEndReason reason) {
    if (m_currentState == GameState::End) return;
    m_currentState = GameState::End;
    m_clock->stop();
    m_elapsedTimer->stop();

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
    emit stateChanged(m_currentState);
}

void GameEngine::resumeGame() {
    if (m_currentState != GameState::Paused) return;
    m_currentState = GameState::Playing;
    m_clock->resume();
    m_elapsedTimer->start(1000);
    emit stateChanged(m_currentState);
}

std::vector<Move> GameEngine::getLegalMoves(int x, int y) {
    std::vector<Move> moves;
    if (m_currentState != GameState::Playing) return moves;

    const auto piece = m_board.getPiece(x, y);
    if (!piece || piece->getOwner() != getCurrentPlayer()) return moves;

    for (int tx = 0; tx < GameConstants::COLS; ++tx) {
        for (int ty = 0; ty < GameConstants::ROWS; ++ty) {
            const Move move = Move::makeMove(x, y, tx, ty, piece->getOwner());
            if (m_ruleEngine.validateMove(m_board, move)) moves.push_back(move);
        }
    }
    return moves;
}

std::vector<Move> GameEngine::getLegalDrops(PieceType type) {
    std::vector<Move> moves;
    if (m_currentState != GameState::Playing) return moves;

    const Player player = getCurrentPlayer();
    if (!m_board.hasDroppablePiece(player, type)) return moves;

    for (int x = 0; x < GameConstants::COLS; ++x) {
        for (int y = 0; y < GameConstants::ROWS; ++y) {
            const Move move = Move::makeDrop(x, y, type, player);
            if (m_ruleEngine.validateMove(m_board, move)) moves.push_back(move);
        }
    }
    return moves;
}

GameState GameEngine::getCurrentState() const { return m_currentState; }
const Board& GameEngine::getBoard() const { return m_board; }
const MoveHistory& GameEngine::getHistory() const { return m_history; }

std::shared_ptr<Piece> GameEngine::createPiece(PieceType type, Player owner) {
    switch (type) {
        case PieceType::King: return std::make_shared<King>(owner);
        case PieceType::Rook: return std::make_shared<Rook>(owner);
        case PieceType::Bishop: return std::make_shared<Bishop>(owner);
        case PieceType::Pawn: return std::make_shared<Pawn>(owner);
        case PieceType::Hou: return std::make_shared<Hou>(owner);
    }
    return nullptr;
}

Player GameEngine::getCurrentPlayer() const {
    if (!m_history.peek().has_value()) return Player::Sente;
    return m_history.peek()->move.player == Player::Sente
               ? Player::Gote
               : Player::Sente;
}
