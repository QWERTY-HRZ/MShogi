#pragma once

#include <optional>
#include <string>
#include <vector>
#include "Board.h"
#include "RuleEngine.h"

enum class CoreEndReason {
    None,
    KingCaptured,
    BaselineEntry,
    NoLegalAction
};

struct CoreMoveResult {
    bool accepted = false;
    PieceType movedType = PieceType::Pawn;
    std::optional<PieceType> capturedType;
    bool promoted = false;
    bool terminal = false;
    int winner = 0;
    CoreEndReason endReason = CoreEndReason::None;
    bool senteThreatened = false;
    bool goteThreatened = false;
};

class GameCore {
public:
    GameCore();

    void reset();
    bool isLegalAction(const Move& move) const;
    bool applyAction(const Move& move, CoreMoveResult* result = nullptr);
    bool undoAction();

    std::vector<Move> legalActions() const;
    std::vector<Move> legalMovesFrom(int x, int y) const;
    std::vector<Move> legalDrops(PieceType type) const;

    const Board& board() const { return m_board; }
    Player currentPlayer() const { return m_currentPlayer; }
    bool isTerminal() const { return m_terminal; }
    int winner() const { return m_winner; }
    CoreEndReason endReason() const { return m_endReason; }
    std::size_t plyCount() const { return m_history.size(); }

    bool isKingThreatened(Player player) const;
    std::string serializeState() const;
    GameCore fork() const;

private:
    struct Snapshot {
        Board board;
        Player currentPlayer;
        bool terminal;
        int winner;
        CoreEndReason endReason;
    };

    Board m_board;
    RuleEngine m_ruleEngine;
    Player m_currentPlayer = Player::Sente;
    bool m_terminal = false;
    int m_winner = 0;
    CoreEndReason m_endReason = CoreEndReason::None;
    std::vector<Snapshot> m_history;
};
