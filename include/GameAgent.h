#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "GameCore.h"

struct ScoredAction {
    Move move;
    double score = 0.0;
};

class GameAgent {
public:
    virtual ~GameAgent() = default;
    virtual std::string name() const = 0;
    virtual std::optional<Move> chooseAction(const GameCore& core) const = 0;
};

struct EvaluationWeights {
    double rook = 5.2;
    double bishop = 5.0;
    double hou = 3.2;
    double pawn = 1.0;
    double handDiscount = 0.85;
    double promotionProgress = 0.12;
    double kingAdvance = 0.10;
    double threatenedKing = 1.25;
};

class AlphaBetaAgent final : public GameAgent {
public:
    explicit AlphaBetaAgent(int maxDepth = 3,
                            EvaluationWeights weights = {});

    std::string name() const override { return "AlphaBeta-Baseline"; }
    std::optional<Move> chooseAction(const GameCore& core) const override;
    std::optional<Move> chooseActionWithTemperature(const GameCore& core,
                                                    double temperature,
                                                    std::uint64_t seed) const;
    std::vector<ScoredAction> scoreActions(const GameCore& core) const;
    static std::optional<Move> selectAction(const std::vector<ScoredAction>& actions,
                                            double temperature,
                                            std::uint64_t seed);

    int maxDepth() const { return m_maxDepth; }
    double evaluate(const GameCore& core, Player perspective) const;

private:
    double search(const GameCore& core, int depth, double alpha, double beta,
                  Player perspective) const;
    double pieceValue(PieceType type) const;

    int m_maxDepth;
    EvaluationWeights m_weights;
};
