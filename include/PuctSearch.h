#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
#include "PolicyValueEvaluator.h"
#include "SelfPlay.h"

struct PuctConfig {
    int simulations = 16;
    int leavesPerBatch = 1;
    double exploration = 1.5;
    double dirichletAlpha = 0.3;
    double dirichletEpsilon = 0.25;
    double virtualLoss = 1.0;
};

struct ActionVisit {
    Move move;
    int visits = 0;
};

struct PuctResult {
    Move selectedAction;
    std::vector<ActionVisit> actionVisits;
    int rootVisits = 0;
    bool reusedTree = false;
};

struct PuctStatistics {
    std::uint64_t searches = 0;
    std::uint64_t simulations = 0;
    std::uint64_t inferenceBatches = 0;
    std::uint64_t inferencePositions = 0;
    std::uint64_t maxInferenceBatch = 0;
    std::uint64_t treeReuseHits = 0;
};

class PuctBatchSearch {
public:
    PuctBatchSearch(const PolicyValueEvaluator& evaluator, PuctConfig config,
                    std::size_t slots);
    ~PuctBatchSearch();

    std::vector<std::optional<PuctResult>> search(
        const std::vector<const GameCore*>& positions,
        const std::vector<std::uint64_t>& seeds,
        const std::vector<double>& temperatures);
    void advance(std::size_t slot, const Move& selectedAction,
                 const GameCore& resultingPosition);
    const PuctStatistics& statistics() const { return m_statistics; }

private:
    struct Node;
    struct PendingLeaf;

    void synchronizeRoot(std::size_t slot, const GameCore& position);
    PendingLeaf selectLeaf(Node& root);
    void expand(Node& node, const PolicyValuePrediction& prediction);
    void addRootNoise(Node& root, std::uint64_t seed) const;
    static void reservePath(const std::vector<Node*>& path, double virtualLoss);
    static void releasePath(const std::vector<Node*>& path, double virtualLoss);
    static void backpropagate(const std::vector<Node*>& path, double value);
    static double terminalValue(const GameCore& position);
    static Move selectByVisits(const Node& root, double temperature,
                               std::uint64_t seed);

    const PolicyValueEvaluator& m_evaluator;
    PuctConfig m_config;
    std::vector<std::unique_ptr<Node>> m_roots;
    std::vector<bool> m_reusedForSearch;
    PuctStatistics m_statistics;
};
