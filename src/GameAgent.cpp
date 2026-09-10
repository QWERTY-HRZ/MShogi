#include "GameAgent.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>

namespace {
int winnerFor(Player player) {
    return player == Player::Sente ? 1 : 2;
}

Player opponent(Player player) {
    return player == Player::Sente ? Player::Gote : Player::Sente;
}
}

AlphaBetaAgent::AlphaBetaAgent(int maxDepth, EvaluationWeights weights)
    : m_maxDepth(std::max(1, maxDepth)), m_weights(weights) {}

std::optional<Move> AlphaBetaAgent::chooseAction(const GameCore& core) const {
    const auto scoredActions = scoreActions(core);
    if (scoredActions.empty()) return std::nullopt;
    return std::max_element(scoredActions.begin(), scoredActions.end(),
                            [](const ScoredAction& lhs, const ScoredAction& rhs) {
        return lhs.score < rhs.score;
    })->move;
}

std::vector<ScoredAction> AlphaBetaAgent::scoreActions(const GameCore& core) const {
    std::vector<ScoredAction> scoredActions;
    const auto actions = core.legalActions();
    scoredActions.reserve(actions.size());
    const Player perspective = core.currentPlayer();
    for (const Move& move : actions) {
        GameCore child = core.fork();
        if (!child.applyAction(move)) continue;
        const double score = search(child, m_maxDepth - 1,
                                    -std::numeric_limits<double>::infinity(),
                                    std::numeric_limits<double>::infinity(),
                                    perspective);
        scoredActions.push_back({move, score});
    }
    return scoredActions;
}

std::optional<Move> AlphaBetaAgent::chooseActionWithTemperature(
    const GameCore& core, double temperature, std::uint64_t seed) const {
    const auto scoredActions = scoreActions(core);
    return selectAction(scoredActions, temperature, seed);
}

std::optional<Move> AlphaBetaAgent::selectAction(
    const std::vector<ScoredAction>& scoredActions,
    double temperature, std::uint64_t seed) {
    if (scoredActions.empty()) return std::nullopt;
    if (temperature <= 0.0) {
        return std::max_element(scoredActions.begin(), scoredActions.end(),
                                [](const ScoredAction& lhs, const ScoredAction& rhs) {
            return lhs.score < rhs.score;
        })->move;
    }

    const double bestScore = std::max_element(
        scoredActions.begin(), scoredActions.end(),
        [](const ScoredAction& lhs, const ScoredAction& rhs) {
            return lhs.score < rhs.score;
        })->score;
    std::vector<double> weights;
    weights.reserve(scoredActions.size());
    for (const auto& action : scoredActions) {
        weights.push_back(std::exp((action.score - bestScore) / temperature));
    }
    std::mt19937_64 generator(seed);
    std::discrete_distribution<std::size_t> distribution(weights.begin(), weights.end());
    return scoredActions[distribution(generator)].move;
}

double AlphaBetaAgent::search(const GameCore& core, int depth,
                              double alpha, double beta,
                              Player perspective) const {
    if (depth == 0 || core.isTerminal()) return evaluate(core, perspective);
    const auto actions = core.legalActions();
    if (actions.empty()) return evaluate(core, perspective);

    const bool maximizing = core.currentPlayer() == perspective;
    double best = maximizing ? -std::numeric_limits<double>::infinity()
                             : std::numeric_limits<double>::infinity();
    for (const Move& move : actions) {
        GameCore child = core.fork();
        if (!child.applyAction(move)) continue;
        const double score = search(child, depth - 1, alpha, beta, perspective);
        if (maximizing) {
            best = std::max(best, score);
            alpha = std::max(alpha, best);
        } else {
            best = std::min(best, score);
            beta = std::min(beta, best);
        }
        if (beta <= alpha) break;
    }
    return best;
}

double AlphaBetaAgent::evaluate(const GameCore& core, Player perspective) const {
    if (core.isTerminal()) {
        return core.winner() == winnerFor(perspective) ? 100000.0 : -100000.0;
    }

    double score = 0.0;
    const Player rival = opponent(perspective);
    for (int x = 0; x < GameConstants::COLS; ++x) {
        for (int y = 0; y < GameConstants::ROWS; ++y) {
            const auto piece = core.board().getPiece(x, y);
            if (!piece || piece->getType() == PieceType::King) continue;
            double value = pieceValue(piece->getType());
            if (piece->getType() == PieceType::Pawn) {
                const int progress = piece->getOwner() == Player::Sente
                                         ? GameConstants::SENTE_BASE_Y - y
                                         : y - GameConstants::GOTE_BASE_Y;
                value += progress * m_weights.promotionProgress;
            }
            score += piece->getOwner() == perspective ? value : -value;
        }
    }

    for (const Player player : {perspective, rival}) {
        const double sign = player == perspective ? 1.0 : -1.0;
        for (const auto& piece : core.board().getHand(player)) {
            score += sign * pieceValue(piece->getType()) * m_weights.handDiscount;
        }
    }

    const auto ownKing = core.board().findPieces(perspective, PieceType::King);
    const auto rivalKing = core.board().findPieces(rival, PieceType::King);
    if (!ownKing.empty()) {
        const int advance = perspective == Player::Sente
                                ? GameConstants::SENTE_BASE_Y - ownKing.front().second
                                : ownKing.front().second - GameConstants::GOTE_BASE_Y;
        score += advance * m_weights.kingAdvance;
    }
    if (!rivalKing.empty()) {
        const int advance = rival == Player::Sente
                                ? GameConstants::SENTE_BASE_Y - rivalKing.front().second
                                : rivalKing.front().second - GameConstants::GOTE_BASE_Y;
        score -= advance * m_weights.kingAdvance;
    }
    if (core.isKingThreatened(perspective)) score -= m_weights.threatenedKing;
    if (core.isKingThreatened(rival)) score += m_weights.threatenedKing;
    return score;
}

double AlphaBetaAgent::pieceValue(PieceType type) const {
    switch (type) {
        case PieceType::Rook: return m_weights.rook;
        case PieceType::Bishop: return m_weights.bishop;
        case PieceType::Hou: return m_weights.hou;
        case PieceType::Pawn: return m_weights.pawn;
        case PieceType::King: return 0.0;
    }
    return 0.0;
}
