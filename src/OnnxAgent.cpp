#include "OnnxAgent.h"

#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>
#include "PolicyValueEvaluator.h"
#include "SelfPlay.h"

namespace {
int winnerFor(Player player) {
    return player == Player::Sente ? 1 : 2;
}
}

class OnnxAgent::Impl {
public:
    Impl(const std::filesystem::path& runtimePath,
         const std::filesystem::path& modelPath,
         int topK, float policyWeight, float valueWeight)
        : m_evaluator(runtimePath, modelPath), m_topK(topK),
          m_policyWeight(policyWeight), m_valueWeight(valueWeight) {
        if (topK <= 0 || policyWeight < 0.0f || valueWeight < 0.0f ||
            (policyWeight == 0.0f && valueWeight == 0.0f)) {
            throw std::invalid_argument("无效的神经网络选招参数");
        }
    }

    std::optional<Move> chooseAction(const GameCore& core) const {
        const auto actions = core.legalActions();
        if (actions.empty()) return std::nullopt;
        const Player mover = core.currentPlayer();

        for (const Move& move : actions) {
            GameCore child = core.fork();
            if (child.applyAction(move) && child.isTerminal() &&
                child.winner() == winnerFor(mover)) {
                // 立即胜着不受 top-k 截断影响，避免模型漏掉确定性终局。
                return move;
            }
        }

        const auto rootPredictions = m_evaluator.evaluate({core.fork()});
        const auto& root = rootPredictions.front();
        std::vector<std::size_t> order(actions.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](std::size_t lhs, std::size_t rhs) {
            return root.policyLogits[encodeAction(actions[lhs])] >
                   root.policyLogits[encodeAction(actions[rhs])];
        });
        order.resize(std::min<std::size_t>(order.size(), m_topK));

        std::vector<float> candidateValues(order.size(), 0.0f);
        std::vector<GameCore> children;
        std::vector<std::size_t> inferredCandidates;
        for (std::size_t candidate = 0; candidate < order.size(); ++candidate) {
            GameCore child = core.fork();
            if (!child.applyAction(actions[order[candidate]])) continue;
            if (child.isTerminal()) {
                candidateValues[candidate] = child.winner() == 0 ? 0.0f :
                    (child.winner() == winnerFor(mover) ? 1.0f : -1.0f);
            } else {
                children.push_back(std::move(child));
                inferredCandidates.push_back(candidate);
            }
        }
        if (!children.empty()) {
            const auto childResults = m_evaluator.evaluate(children);
            for (std::size_t index = 0; index < inferredCandidates.size(); ++index) {
                // 子局面轮到对手，取负号还原为当前走子方价值。
                candidateValues[inferredCandidates[index]] = -childResults[index].value;
            }
        }

        const float bestPolicy = root.policyLogits[encodeAction(actions[order.front()])];
        std::size_t bestCandidate = 0;
        float bestScore = -std::numeric_limits<float>::infinity();
        for (std::size_t candidate = 0; candidate < order.size(); ++candidate) {
            const float policy = root.policyLogits[encodeAction(actions[order[candidate]])];
            const float score = m_policyWeight * (policy - bestPolicy) +
                                m_valueWeight * candidateValues[candidate];
            if (score > bestScore) {
                bestScore = score;
                bestCandidate = candidate;
            }
        }
        return actions[order[bestCandidate]];
    }

private:
    OnnxEvaluator m_evaluator;
    std::size_t m_topK;
    float m_policyWeight;
    float m_valueWeight;
};

OnnxAgent::OnnxAgent(std::filesystem::path runtimePath,
                     std::filesystem::path modelPath,
                     int topK, float policyWeight, float valueWeight)
    : m_impl(std::make_unique<Impl>(runtimePath, modelPath, topK,
                                    policyWeight, valueWeight)) {}

OnnxAgent::~OnnxAgent() = default;

std::optional<Move> OnnxAgent::chooseAction(const GameCore& core) const {
    try {
        return m_impl->chooseAction(core);
    } catch (const std::exception&) {
        // 推理失败时保持对局可继续，下一着退回规则一致的深度 2 搜索。
        return m_fallback.chooseAction(core);
    }
}
