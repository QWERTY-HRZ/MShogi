#include "OnnxAgent.h"

#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>

#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>
#include "SelfPlay.h"

namespace {
constexpr std::size_t kBoardSize = 10 * GameConstants::ROWS * GameConstants::COLS;
constexpr std::size_t kHandSize = 2 * 3 * 4;
constexpr std::size_t kMetaSize = 5;
constexpr std::size_t kActionCount = 990;

using GetApiBaseFunction = const OrtApiBase* (ORT_API_CALL*)();

void initializeRuntime(const std::filesystem::path& runtimePath) {
    static std::once_flag once;
    static HMODULE module = nullptr;
    std::call_once(once, [&runtimePath] {
        module = LoadLibraryW(std::filesystem::absolute(runtimePath).c_str());
        if (!module) throw std::runtime_error("无法加载 onnxruntime.dll");
        const auto getApiBase = reinterpret_cast<GetApiBaseFunction>(
            GetProcAddress(module, "OrtGetApiBase"));
        if (!getApiBase) throw std::runtime_error("ONNX Runtime 缺少 OrtGetApiBase");
        const OrtApiBase* base = getApiBase();
        const OrtApi* api = base ? base->GetApi(ORT_API_VERSION) : nullptr;
        if (!api) throw std::runtime_error("ONNX Runtime API 版本不兼容");
        // 动态取得 API 表，MinGW 客户端无需链接 MSVC 导入库。
        Ort::InitApi(api);
    });
}

int pieceChannel(PieceType type) {
    switch (type) {
        case PieceType::King: return 0;
        case PieceType::Rook: return 1;
        case PieceType::Bishop: return 2;
        case PieceType::Pawn: return 3;
        case PieceType::Hou: return 4;
    }
    throw std::runtime_error("未知棋子类型");
}

int handType(PieceType type) {
    switch (type) {
        case PieceType::Rook: return 0;
        case PieceType::Bishop: return 1;
        case PieceType::Pawn: return 2;
        default: return -1;
    }
}

int winnerFor(Player player) {
    return player == Player::Sente ? 1 : 2;
}

struct EncodedPosition {
    std::array<float, kBoardSize> board{};
    std::array<float, kHandSize> hand{};
    std::array<float, kMetaSize> meta{};
};

EncodedPosition encodePosition(const GameCore& core) {
    EncodedPosition encoded;
    for (int y = 0; y < GameConstants::ROWS; ++y) {
        for (int x = 0; x < GameConstants::COLS; ++x) {
            const auto piece = core.board().getPiece(x, y);
            if (!piece) continue;
            const int ownerOffset = piece->getOwner() == Player::Sente ? 0 : 5;
            const int channel = ownerOffset + pieceChannel(piece->getType());
            encoded.board[channel * 30 + y * GameConstants::COLS + x] = 1.0f;
        }
    }
    for (const Player player : {Player::Sente, Player::Gote}) {
        const int owner = player == Player::Sente ? 0 : 1;
        for (const auto& piece : core.board().getHand(player)) {
            const int type = handType(piece->getType());
            const int cooldown = piece->getTurnsInHand();
            if (type < 0 || cooldown < 1 || cooldown > 4) {
                throw std::runtime_error("手驹状态无法编码");
            }
            encoded.hand[(owner * 3 + type) * 4 + cooldown - 1] += 1.0f;
        }
    }
    encoded.meta[0] = core.currentPlayer() == Player::Sente ? 1.0f : 0.0f;
    encoded.meta[1] = core.currentPlayer() == Player::Gote ? 1.0f : 0.0f;
    encoded.meta[2] = core.board().getKingInBaseFlag(Player::Sente) ? 1.0f : 0.0f;
    encoded.meta[3] = core.board().getKingInBaseFlag(Player::Gote) ? 1.0f : 0.0f;
    encoded.meta[4] = std::min(core.currentPositionOccurrences(), 3) / 3.0f;
    return encoded;
}

struct InferenceResult {
    std::vector<float> policy;
    std::vector<float> value;
};
}

class OnnxAgent::Impl {
public:
    Impl(const std::filesystem::path& runtimePath,
         const std::filesystem::path& modelPath,
         int topK, float policyWeight, float valueWeight)
        : m_topK(topK),
          m_policyWeight(policyWeight),
          m_valueWeight(valueWeight) {
        if (!std::filesystem::is_regular_file(runtimePath)) {
            throw std::runtime_error("找不到 onnxruntime.dll");
        }
        if (!std::filesystem::is_regular_file(modelPath)) {
            throw std::runtime_error("找不到神经网络模型");
        }
        if (topK <= 0 || policyWeight < 0.0f || valueWeight < 0.0f ||
            (policyWeight == 0.0f && valueWeight == 0.0f)) {
            throw std::invalid_argument("无效的神经网络选招参数");
        }
        initializeRuntime(runtimePath);
        m_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "MShogi");
        m_memory = std::make_unique<Ort::MemoryInfo>(
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));
        m_options = std::make_unique<Ort::SessionOptions>();
        m_options->SetIntraOpNumThreads(4);
        m_options->SetInterOpNumThreads(1);
        m_options->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        m_session = std::make_unique<Ort::Session>(
            *m_env, std::filesystem::absolute(modelPath).c_str(), *m_options);
        validateModel();
    }

    std::optional<Move> chooseAction(const GameCore& core) const {
        const auto actions = core.legalActions();
        if (actions.empty()) return std::nullopt;
        const Player mover = core.currentPlayer();

        for (const Move& move : actions) {
            GameCore child = core.fork();
            if (child.applyAction(move) && child.isTerminal() &&
                child.winner() == winnerFor(mover)) {
                // 立即胜着不受 top-k 截断影响，避免模型漏掉确定性吃王或下底胜利。
                return move;
            }
        }

        const InferenceResult root = infer({encodePosition(core)});
        std::vector<std::size_t> order(actions.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](std::size_t lhs, std::size_t rhs) {
            return root.policy[encodeAction(actions[lhs])] >
                   root.policy[encodeAction(actions[rhs])];
        });
        order.resize(std::min<std::size_t>(order.size(), m_topK));

        std::vector<float> candidateValues(order.size(), 0.0f);
        std::vector<EncodedPosition> children;
        std::vector<std::size_t> inferredCandidates;
        for (std::size_t candidate = 0; candidate < order.size(); ++candidate) {
            GameCore child = core.fork();
            if (!child.applyAction(actions[order[candidate]])) continue;
            if (child.isTerminal()) {
                candidateValues[candidate] = child.winner() == 0 ? 0.0f :
                    (child.winner() == winnerFor(mover) ? 1.0f : -1.0f);
            } else {
                children.push_back(encodePosition(child));
                inferredCandidates.push_back(candidate);
            }
        }
        if (!children.empty()) {
            const InferenceResult childResults = infer(children);
            for (std::size_t index = 0; index < inferredCandidates.size(); ++index) {
                // 子局面轮到对手，取负号还原为当前走子方价值。
                candidateValues[inferredCandidates[index]] = -childResults.value[index];
            }
        }

        const float bestPolicy = root.policy[encodeAction(actions[order.front()])];
        std::size_t bestCandidate = 0;
        float bestScore = -std::numeric_limits<float>::infinity();
        for (std::size_t candidate = 0; candidate < order.size(); ++candidate) {
            const float policy = root.policy[encodeAction(actions[order[candidate]])];
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
    void validateModel() const {
        if (m_session->GetInputCount() != 3 || m_session->GetOutputCount() != 2) {
            throw std::runtime_error("模型输入输出数量不兼容");
        }
        Ort::AllocatorWithDefaultOptions allocator;
        const std::array<const char*, 3> expectedInputs{"board", "hand", "meta"};
        const std::array<const char*, 2> expectedOutputs{"policy_logits", "value"};
        for (std::size_t index = 0; index < expectedInputs.size(); ++index) {
            const auto name = m_session->GetInputNameAllocated(index, allocator);
            if (std::string(name.get()) != expectedInputs[index]) {
                throw std::runtime_error("模型输入名称不兼容");
            }
        }
        for (std::size_t index = 0; index < expectedOutputs.size(); ++index) {
            const auto name = m_session->GetOutputNameAllocated(index, allocator);
            if (std::string(name.get()) != expectedOutputs[index]) {
                throw std::runtime_error("模型输出名称不兼容");
            }
        }
    }

    InferenceResult infer(const std::vector<EncodedPosition>& positions) const {
        const std::size_t batch = positions.size();
        std::vector<float> boards(batch * kBoardSize);
        std::vector<float> hands(batch * kHandSize);
        std::vector<float> metas(batch * kMetaSize);
        for (std::size_t index = 0; index < batch; ++index) {
            std::copy(positions[index].board.begin(), positions[index].board.end(),
                      boards.begin() + index * kBoardSize);
            std::copy(positions[index].hand.begin(), positions[index].hand.end(),
                      hands.begin() + index * kHandSize);
            std::copy(positions[index].meta.begin(), positions[index].meta.end(),
                      metas.begin() + index * kMetaSize);
        }
        const std::array<std::int64_t, 4> boardShape{
            static_cast<std::int64_t>(batch), 10, 6, 5};
        const std::array<std::int64_t, 4> handShape{
            static_cast<std::int64_t>(batch), 2, 3, 4};
        const std::array<std::int64_t, 2> metaShape{
            static_cast<std::int64_t>(batch), 5};
        std::array<Ort::Value, 3> inputs{
            Ort::Value::CreateTensor<float>(*m_memory, boards.data(), boards.size(),
                                            boardShape.data(), boardShape.size()),
            Ort::Value::CreateTensor<float>(*m_memory, hands.data(), hands.size(),
                                            handShape.data(), handShape.size()),
            Ort::Value::CreateTensor<float>(*m_memory, metas.data(), metas.size(),
                                            metaShape.data(), metaShape.size()),
        };
        const std::array<const char*, 3> inputNames{"board", "hand", "meta"};
        const std::array<const char*, 2> outputNames{"policy_logits", "value"};
        auto outputs = m_session->Run(Ort::RunOptions{nullptr}, inputNames.data(),
                                      inputs.data(), inputs.size(), outputNames.data(),
                                      outputNames.size());
        const auto policyInfo = outputs[0].GetTensorTypeAndShapeInfo();
        const auto valueInfo = outputs[1].GetTensorTypeAndShapeInfo();
        if (policyInfo.GetElementCount() != batch * kActionCount ||
            valueInfo.GetElementCount() != batch) {
            throw std::runtime_error("模型输出形状不兼容");
        }
        const float* policy = outputs[0].GetTensorData<float>();
        const float* value = outputs[1].GetTensorData<float>();
        if (!std::all_of(policy, policy + batch * kActionCount,
                         [](float item) { return std::isfinite(item); }) ||
            !std::all_of(value, value + batch,
                         [](float item) { return std::isfinite(item); })) {
            throw std::runtime_error("模型输出包含非有限值");
        }
        return {{policy, policy + batch * kActionCount}, {value, value + batch}};
    }

    std::unique_ptr<Ort::Env> m_env;
    std::unique_ptr<Ort::SessionOptions> m_options;
    std::unique_ptr<Ort::MemoryInfo> m_memory;
    std::unique_ptr<Ort::Session> m_session;
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
