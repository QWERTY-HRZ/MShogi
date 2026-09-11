#include "GameCore.h"
#include <algorithm>
#include <sstream>

namespace {
Player opponent(Player player) {
    return player == Player::Sente ? Player::Gote : Player::Sente;
}

char pieceCode(PieceType type) {
    switch (type) {
        case PieceType::King: return 'K';
        case PieceType::Rook: return 'R';
        case PieceType::Bishop: return 'B';
        case PieceType::Pawn: return 'P';
        case PieceType::Hou: return 'H';
    }
    return '?';
}
}

GameCore::GameCore() {
    reset();
}

void GameCore::reset() {
    m_board.clear();
    m_history.clear();
    m_positionOccurrences.clear();
    m_currentPlayer = Player::Sente;
    m_terminal = false;
    m_winner = 0;
    m_endReason = CoreEndReason::None;

    const int senteY = GameConstants::SENTE_BASE_Y;
    const int goteY = GameConstants::GOTE_BASE_Y;
    m_board.placePiece(2, senteY, makePiece(PieceType::King, Player::Sente));
    m_board.placePiece(0, senteY, makePiece(PieceType::Bishop, Player::Sente));
    m_board.placePiece(4, senteY, makePiece(PieceType::Rook, Player::Sente));
    m_board.placePiece(0, senteY - 1, makePiece(PieceType::Pawn, Player::Sente));
    m_board.placePiece(2, senteY - 1, makePiece(PieceType::Pawn, Player::Sente));
    m_board.placePiece(4, senteY - 1, makePiece(PieceType::Pawn, Player::Sente));

    m_board.placePiece(2, goteY, makePiece(PieceType::King, Player::Gote));
    m_board.placePiece(0, goteY, makePiece(PieceType::Rook, Player::Gote));
    m_board.placePiece(4, goteY, makePiece(PieceType::Bishop, Player::Gote));
    m_board.placePiece(0, goteY + 1, makePiece(PieceType::Pawn, Player::Gote));
    m_board.placePiece(2, goteY + 1, makePiece(PieceType::Pawn, Player::Gote));
    m_board.placePiece(4, goteY + 1, makePiece(PieceType::Pawn, Player::Gote));

    // 初始局面计作第一次出现，第三次轮到同一方时立即和棋。
    m_positionOccurrences[positionKey()] = 1;
}

bool GameCore::isLegalAction(const Move& move) const {
    return !m_terminal && move.player == m_currentPlayer &&
           m_ruleEngine.validateMove(m_board, move);
}

bool GameCore::applyAction(const Move& move, CoreMoveResult* result) {
    CoreMoveResult localResult;
    if (!isLegalAction(move)) {
        if (result) *result = localResult;
        return false;
    }

    const auto movingPiece = move.isDrop ? nullptr : m_board.getPiece(move.fromX, move.fromY);
    const auto target = move.isDrop ? nullptr : m_board.getPiece(move.toX, move.toY);
    localResult.movedType = move.isDrop ? move.dropType : movingPiece->getType();
    if (target) localResult.capturedType = target->getType();
    localResult.promoted = !move.isDrop && m_ruleEngine.checkPromotion(m_board, move);

    Snapshot snapshot{m_board, m_currentPlayer, m_terminal, m_winner, m_endReason};
    if (move.isDrop) {
        auto droppedPiece = m_board.takeFromHand(move.player, move.dropType);
        if (!droppedPiece || !m_board.placePiece(move.toX, move.toY, droppedPiece)) {
            if (droppedPiece) m_board.addToHand(droppedPiece);
            if (result) *result = localResult;
            return false;
        }
    } else {
        if (target && target->getType() != PieceType::King) {
            const PieceType handType = target->getType() == PieceType::Hou
                                           ? PieceType::Pawn
                                           : target->getType();
            auto handPiece = makePiece(handType, move.player);
            handPiece->setTurnsInHand(1);
            m_board.addToHand(handPiece);
        }
        if (!m_board.movePiece(move.fromX, move.fromY, move.toX, move.toY)) {
            m_board = snapshot.board;
            if (result) *result = localResult;
            return false;
        }
        if (localResult.promoted) {
            m_board.removePiece(move.toX, move.toY);
            m_board.placePiece(move.toX, move.toY,
                               makePiece(PieceType::Hou, move.player));
        }
    }

    m_board.advanceHandTurns();
    m_history.push_back(std::move(snapshot));
    m_currentPlayer = opponent(move.player);
    localResult.accepted = true;

    const int boardWinner = m_ruleEngine.isGameOver(m_board);
    // 下底检测可能更新标记，必须在标记稳定后登记重复局面。
    const int positionOccurrences = ++m_positionOccurrences[positionKey()];
    if (boardWinner != 0) {
        m_terminal = true;
        m_winner = boardWinner;
        m_endReason = localResult.capturedType == PieceType::King
                          ? CoreEndReason::KingCaptured
                          : CoreEndReason::BaselineEntry;
    } else if (!m_ruleEngine.hasAnyLegalAction(m_board, m_currentPlayer)) {
        m_terminal = true;
        m_winner = move.player == Player::Sente ? 1 : 2;
        m_endReason = CoreEndReason::NoLegalAction;
    } else if (positionOccurrences >= 3) {
        // 决胜条件优先；仅未分胜负的第三次相同局面判和。
        m_terminal = true;
        m_winner = 0;
        m_endReason = CoreEndReason::RepetitionDraw;
    }

    localResult.terminal = m_terminal;
    localResult.winner = m_winner;
    localResult.endReason = m_endReason;
    localResult.senteThreatened = isKingThreatened(Player::Sente);
    localResult.goteThreatened = isKingThreatened(Player::Gote);
    if (result) *result = localResult;
    return true;
}

bool GameCore::undoAction() {
    if (m_history.empty()) return false;
    const std::string currentKey = positionKey();
    auto occurrence = m_positionOccurrences.find(currentKey);
    if (occurrence != m_positionOccurrences.end() && --occurrence->second == 0) {
        m_positionOccurrences.erase(occurrence);
    }
    Snapshot snapshot = std::move(m_history.back());
    m_history.pop_back();
    m_board = std::move(snapshot.board);
    m_currentPlayer = snapshot.currentPlayer;
    m_terminal = snapshot.terminal;
    m_winner = snapshot.winner;
    m_endReason = snapshot.endReason;
    return true;
}

std::vector<Move> GameCore::legalActions() const {
    std::vector<Move> actions;
    if (m_terminal) return actions;

    for (int x = 0; x < GameConstants::COLS; ++x) {
        for (int y = 0; y < GameConstants::ROWS; ++y) {
            auto moves = legalMovesFrom(x, y);
            actions.insert(actions.end(), moves.begin(), moves.end());
        }
    }
    for (const PieceType type : {PieceType::Rook, PieceType::Bishop, PieceType::Pawn}) {
        auto drops = legalDrops(type);
        actions.insert(actions.end(), drops.begin(), drops.end());
    }
    return actions;
}

std::vector<Move> GameCore::legalMovesFrom(int x, int y) const {
    std::vector<Move> moves;
    if (m_terminal) return moves;
    const auto piece = m_board.getPiece(x, y);
    if (!piece || piece->getOwner() != m_currentPlayer) return moves;

    for (int toX = 0; toX < GameConstants::COLS; ++toX) {
        for (int toY = 0; toY < GameConstants::ROWS; ++toY) {
            const Move move = Move::makeMove(x, y, toX, toY, m_currentPlayer);
            if (m_ruleEngine.validateMove(m_board, move)) moves.push_back(move);
        }
    }
    return moves;
}

std::vector<Move> GameCore::legalDrops(PieceType type) const {
    std::vector<Move> drops;
    if (m_terminal || !m_board.hasDroppablePiece(m_currentPlayer, type)) return drops;
    for (int x = 0; x < GameConstants::COLS; ++x) {
        for (int y = 0; y < GameConstants::ROWS; ++y) {
            const Move move = Move::makeDrop(x, y, type, m_currentPlayer);
            if (m_ruleEngine.validateMove(m_board, move)) drops.push_back(move);
        }
    }
    return drops;
}

bool GameCore::isKingThreatened(Player player) const {
    return m_ruleEngine.isKingThreatened(m_board, player);
}

std::string GameCore::positionKey() const {
    std::ostringstream output;
    output << "turn=" << (m_currentPlayer == Player::Sente ? 'S' : 'G');
    output << "|board=";
    for (int y = 0; y < GameConstants::ROWS; ++y) {
        for (int x = 0; x < GameConstants::COLS; ++x) {
            const auto piece = m_board.getPiece(x, y);
            if (!piece) {
                output << "..";
            } else {
                output << (piece->getOwner() == Player::Sente ? 'S' : 'G')
                       << pieceCode(piece->getType());
            }
        }
    }

    for (const Player player : {Player::Sente, Player::Gote}) {
        std::vector<std::string> handTokens;
        for (const auto& piece : m_board.getHand(player)) {
            handTokens.push_back(std::string(1, pieceCode(piece->getType())) +
                                 std::to_string(piece->getTurnsInHand()));
        }
        std::sort(handTokens.begin(), handTokens.end());
        output << (player == Player::Sente ? "|sHand=" : "|gHand=");
        for (const auto& token : handTokens) output << token << ',';
    }

    output << "|flags=" << m_board.getKingInBaseFlag(Player::Sente)
           << m_board.getKingInBaseFlag(Player::Gote);
    return output.str();
}

int GameCore::currentPositionOccurrences() const {
    const auto occurrence = m_positionOccurrences.find(positionKey());
    return occurrence == m_positionOccurrences.end() ? 0 : occurrence->second;
}

std::string GameCore::serializeState() const {
    std::ostringstream output;
    output << GameConstants::RULE_VERSION << '|' << positionKey()
           << "|repetition=" << currentPositionOccurrences()
           << "|terminal=" << m_terminal << "|winner=" << m_winner;
    return output.str();
}

GameCore GameCore::fork() const {
    GameCore copy;
    // 搜索分支只复制当前状态，不复制真实对局的整条撤销历史。
    copy.m_board = m_board;
    copy.m_currentPlayer = m_currentPlayer;
    copy.m_terminal = m_terminal;
    copy.m_winner = m_winner;
    copy.m_endReason = m_endReason;
    // 搜索分支需继承重复历史，否则无法识别分支内的第三次局面。
    copy.m_positionOccurrences = m_positionOccurrences;
    return copy;
}
