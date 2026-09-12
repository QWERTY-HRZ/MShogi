#include "PuctSearch.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>

struct PuctBatchSearch::Node {
    explicit Node(GameCore position) : position(std::move(position)) {}

    GameCore position;
    Move move{};
    double prior = 0.0;
    int visits = 0;
    double valueSum = 0.0;
    bool expanded = false;
    bool pending = false;
    std::vector<std::unique_ptr<Node>> children;
};

struct PuctBatchSearch::PendingLeaf {
    Node* leaf = nullptr;
    std::vector<Node*> path;
};

namespace {
int winnerFor(Player player) {
    return player == Player::Sente ? 1 : 2;
}

bool sameMove(const Move& lhs, const Move& rhs) {
    return lhs.fromX == rhs.fromX && lhs.fromY == rhs.fromY &&
           lhs.toX == rhs.toX && lhs.toY == rhs.toY &&
           lhs.isDrop == rhs.isDrop && lhs.dropType == rhs.dropType &&
           lhs.player == rhs.player;
}
}

PuctBatchSearch::PuctBatchSearch(const PolicyValueEvaluator& evaluator,
                                 PuctConfig config, std::size_t slots)
    : m_evaluator(evaluator), m_config(config), m_roots(slots),
      m_reusedForSearch(slots, false) {
    if (config.simulations < 2 || config.leavesPerBatch <= 0 ||
        config.exploration < 0.0 || config.virtualLoss < 0.0 ||
        config.dirichletAlpha <= 0.0 || config.dirichletEpsilon < 0.0 ||
        config.dirichletEpsilon > 1.0 || slots == 0) {
        throw std::invalid_argument("无效的 PUCT 配置");
    }
}

PuctBatchSearch::~PuctBatchSearch() = default;

void PuctBatchSearch::synchronizeRoot(std::size_t slot, const GameCore& position) {
    if (slot >= m_roots.size()) throw std::out_of_range("PUCT slot 越界");
    const bool reusable = m_roots[slot] &&
                          m_roots[slot]->position.serializeState() == position.serializeState();
    if (!reusable) m_roots[slot] = std::make_unique<Node>(position.fork());
    m_reusedForSearch[slot] = reusable;
    if (reusable) ++m_statistics.treeReuseHits;
}

PuctBatchSearch::PendingLeaf PuctBatchSearch::selectLeaf(Node& root) {
    PendingLeaf pending;
    Node* node = &root;
    pending.path.push_back(node);
    while (node->expanded && !node->position.isTerminal() && !node->children.empty()) {
        Node* best = nullptr;
        double bestScore = -std::numeric_limits<double>::infinity();
        const double parentScale = std::sqrt(std::max(1, node->visits));
        for (auto& childPointer : node->children) {
            Node& child = *childPointer;
            // 子节点价值属于对手视角，PUCT 在父节点选边时必须取负号。
            const double q = child.visits == 0 ? 0.0
                                               : -child.valueSum / child.visits;
            const double u = m_config.exploration * child.prior * parentScale /
                             (1.0 + child.visits);
            const double score = q + u;
            if (!best || score > bestScore ||
                (score == bestScore && encodeAction(child.move) < encodeAction(best->move))) {
                best = &child;
                bestScore = score;
            }
        }
        node = best;
        pending.path.push_back(node);
    }
    pending.leaf = node;
    return pending;
}

void PuctBatchSearch::expand(Node& node, const PolicyValuePrediction& prediction) {
    if (node.expanded || node.position.isTerminal()) return;
    const auto actions = node.position.legalActions();
    if (actions.empty()) {
        node.expanded = true;
        return;
    }
    double maximum = -std::numeric_limits<double>::infinity();
    for (const Move& move : actions) {
        maximum = std::max(maximum,
                           static_cast<double>(prediction.policyLogits[encodeAction(move)]));
    }
    std::vector<double> probabilities;
    probabilities.reserve(actions.size());
    double total = 0.0;
    for (const Move& move : actions) {
        const double probability = std::exp(
            static_cast<double>(prediction.policyLogits[encodeAction(move)]) - maximum);
        probabilities.push_back(probability);
        total += probability;
    }
    node.children.reserve(actions.size());
    for (std::size_t index = 0; index < actions.size(); ++index) {
        GameCore childPosition = node.position.fork();
        if (!childPosition.applyAction(actions[index])) {
            throw std::runtime_error("PUCT 展开得到非法着法");
        }
        auto child = std::make_unique<Node>(std::move(childPosition));
        child->move = actions[index];
        child->prior = probabilities[index] / total;
        node.children.push_back(std::move(child));
    }
    node.expanded = true;
}

void PuctBatchSearch::addRootNoise(Node& root, std::uint64_t seed) const {
    if (!root.expanded || root.children.empty() || m_config.dirichletEpsilon == 0.0) {
        return;
    }
    std::mt19937_64 generator(seed);
    std::gamma_distribution<double> gamma(m_config.dirichletAlpha, 1.0);
    std::vector<double> noise(root.children.size());
    for (double& item : noise) item = gamma(generator);
    const double total = std::accumulate(noise.begin(), noise.end(), 0.0);
    if (total <= 0.0) return;
    for (std::size_t index = 0; index < root.children.size(); ++index) {
        const double normalized = noise[index] / total;
        // 噪声只混入当前根节点，扩大自博弈开局探索而不污染深层先验。
        root.children[index]->prior =
            (1.0 - m_config.dirichletEpsilon) * root.children[index]->prior +
            m_config.dirichletEpsilon * normalized;
    }
}

void PuctBatchSearch::backpropagate(const std::vector<Node*>& path, double value) {
    for (auto iterator = path.rbegin(); iterator != path.rend(); ++iterator) {
        Node& node = **iterator;
        ++node.visits;
        node.valueSum += value;
        // 每深入一层轮到另一方，价值视角随之翻转。
        value = -value;
    }
}

void PuctBatchSearch::reservePath(const std::vector<Node*>& path,
                                  double virtualLoss) {
    for (Node* node : path) {
        ++node->visits;
        // 临时正价值会让父节点看到负 Q，避免同批模拟拥挤到同一分支。
        node->valueSum += virtualLoss;
    }
}

void PuctBatchSearch::releasePath(const std::vector<Node*>& path,
                                  double virtualLoss) {
    for (Node* node : path) {
        --node->visits;
        node->valueSum -= virtualLoss;
    }
}

double PuctBatchSearch::terminalValue(const GameCore& position) {
    if (!position.isTerminal() || position.winner() == 0) return 0.0;
    return position.winner() == winnerFor(position.currentPlayer()) ? 1.0 : -1.0;
}

Move PuctBatchSearch::selectByVisits(const Node& root, double temperature,
                                     std::uint64_t seed) {
    if (root.children.empty()) throw std::runtime_error("PUCT 根节点没有合法着法");
    if (temperature <= 0.0) {
        return (*std::max_element(
            root.children.begin(), root.children.end(),
            [](const auto& lhs, const auto& rhs) {
                if (lhs->visits != rhs->visits) return lhs->visits < rhs->visits;
                return encodeAction(lhs->move) > encodeAction(rhs->move);
            }))->move;
    }
    std::vector<double> weights;
    weights.reserve(root.children.size());
    for (const auto& child : root.children) {
        weights.push_back(std::pow(static_cast<double>(child->visits), 1.0 / temperature));
    }
    if (std::accumulate(weights.begin(), weights.end(), 0.0) <= 0.0) {
        throw std::runtime_error("PUCT 访问次数为空");
    }
    std::mt19937_64 generator(seed);
    std::discrete_distribution<std::size_t> distribution(weights.begin(), weights.end());
    return root.children[distribution(generator)]->move;
}

std::vector<std::optional<PuctResult>> PuctBatchSearch::search(
    const std::vector<const GameCore*>& positions,
    const std::vector<std::uint64_t>& seeds,
    const std::vector<double>& temperatures) {
    if (positions.size() != m_roots.size() || seeds.size() != positions.size() ||
        temperatures.size() != positions.size()) {
        throw std::invalid_argument("PUCT 批量参数数量不一致");
    }
    for (std::size_t slot = 0; slot < positions.size(); ++slot) {
        if (positions[slot]) {
            if (positions[slot]->isTerminal()) {
                throw std::invalid_argument("不能搜索终局");
            }
            synchronizeRoot(slot, *positions[slot]);
            if (temperatures[slot] < 0.0) {
                throw std::invalid_argument("PUCT 温度不能为负数");
            }
            ++m_statistics.searches;
        }
    }

    std::vector<int> completed(positions.size(), 0);
    std::vector<bool> noiseApplied(positions.size(), false);
    for (std::size_t slot = 0; slot < positions.size(); ++slot) {
        if (positions[slot] && m_roots[slot]->expanded) {
            addRootNoise(*m_roots[slot], seeds[slot]);
            noiseApplied[slot] = true;
        }
    }
    while (true) {
        std::vector<PendingLeaf> pending;
        std::vector<GameCore> inferencePositions;
        bool progressed = false;
        for (std::size_t slot = 0; slot < positions.size(); ++slot) {
            if (!positions[slot]) continue;
            const int count = std::min(m_config.leavesPerBatch,
                                       m_config.simulations - completed[slot]);
            for (int leafIndex = 0; leafIndex < count; ++leafIndex) {
                PendingLeaf leaf = selectLeaf(*m_roots[slot]);
                if (leaf.leaf->pending) break;
                if (leaf.leaf->position.isTerminal()) {
                    backpropagate(leaf.path, terminalValue(leaf.leaf->position));
                } else {
                    reservePath(leaf.path, m_config.virtualLoss);
                    leaf.leaf->pending = true;
                    inferencePositions.push_back(leaf.leaf->position.fork());
                    pending.push_back(std::move(leaf));
                }
                ++completed[slot];
                ++m_statistics.simulations;
                progressed = true;
            }
        }
        if (!inferencePositions.empty()) {
            // 跨局与单局多叶共同组成动态 batch，推理后先撤销 virtual loss。
            const auto predictions = m_evaluator.evaluate(inferencePositions);
            if (predictions.size() != pending.size()) {
                throw std::runtime_error("评估器返回数量与叶节点不一致");
            }
            ++m_statistics.inferenceBatches;
            m_statistics.inferencePositions += predictions.size();
            m_statistics.maxInferenceBatch = std::max<std::uint64_t>(
                m_statistics.maxInferenceBatch, predictions.size());
            for (std::size_t index = 0; index < pending.size(); ++index) {
                releasePath(pending[index].path, m_config.virtualLoss);
                pending[index].leaf->pending = false;
                expand(*pending[index].leaf, predictions[index]);
                backpropagate(pending[index].path, predictions[index].value);
            }
        }
        for (std::size_t slot = 0; slot < positions.size(); ++slot) {
            if (positions[slot] && !noiseApplied[slot] && m_roots[slot]->expanded) {
                addRootNoise(*m_roots[slot], seeds[slot]);
                noiseApplied[slot] = true;
            }
        }
        bool done = true;
        for (std::size_t slot = 0; slot < positions.size(); ++slot) {
            if (positions[slot] && completed[slot] != m_config.simulations) {
                done = false;
                break;
            }
        }
        if (done) break;
        if (!progressed) throw std::runtime_error("PUCT 多叶批处理没有取得进展");
    }

    std::vector<std::optional<PuctResult>> results(positions.size());
    for (std::size_t slot = 0; slot < positions.size(); ++slot) {
        if (!positions[slot]) continue;
        Node& root = *m_roots[slot];
        PuctResult result;
        result.selectedAction = selectByVisits(root, temperatures[slot],
                                               seeds[slot] ^ 0xD1B54A32D192ED03ULL);
        result.rootVisits = root.visits;
        result.reusedTree = m_reusedForSearch[slot];
        result.actionVisits.reserve(root.children.size());
        for (const auto& child : root.children) {
            result.actionVisits.push_back({child->move, child->visits});
        }
        results[slot] = std::move(result);
    }
    return results;
}

void PuctBatchSearch::advance(std::size_t slot, const Move& selectedAction,
                              const GameCore& resultingPosition) {
    if (slot >= m_roots.size()) throw std::out_of_range("PUCT slot 越界");
    if (!m_roots[slot]) return;
    for (auto& child : m_roots[slot]->children) {
        if (sameMove(child->move, selectedAction) &&
            child->position.serializeState() == resultingPosition.serializeState()) {
            // 只保留实战分支，下一回合继续利用已展开节点和访问统计。
            m_roots[slot] = std::move(child);
            return;
        }
    }
    m_roots[slot] = std::make_unique<Node>(resultingPosition.fork());
}
