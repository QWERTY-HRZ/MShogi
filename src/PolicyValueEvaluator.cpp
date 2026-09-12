#include "PolicyValueEvaluator.h"

#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>

#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::size_t BoardSize = 10 * GameConstants::ROWS * GameConstants::COLS;
constexpr std::size_t HandSize = 2 * 3 * 4;
constexpr std::size_t MetaSize = 5;

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
        // 动态取得 API 表，MinGW 无需链接 MSVC 导入库。
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

struct EncodedPosition {
    std::array<float, BoardSize> board{};
    std::array<float, HandSize> hand{};
    std::array<float, MetaSize> meta{};
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
}

class OnnxEvaluator::Impl {
public:
    Impl(const std::filesystem::path& runtimePath,
         const std::filesystem::path& modelPath) {
        if (!std::filesystem::is_regular_file(runtimePath)) {
            throw std::runtime_error("找不到 onnxruntime.dll");
        }
        if (!std::filesystem::is_regular_file(modelPath)) {
            throw std::runtime_error("找不到神经网络模型");
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

    std::vector<PolicyValuePrediction> evaluate(
        const std::vector<GameCore>& positions) const {
        if (positions.empty()) return {};
        const std::size_t batch = positions.size();
        std::vector<EncodedPosition> encoded;
        encoded.reserve(batch);
        for (const GameCore& position : positions) encoded.push_back(encodePosition(position));

        std::vector<float> boards(batch * BoardSize);
        std::vector<float> hands(batch * HandSize);
        std::vector<float> metas(batch * MetaSize);
        for (std::size_t index = 0; index < batch; ++index) {
            std::copy(encoded[index].board.begin(), encoded[index].board.end(),
                      boards.begin() + index * BoardSize);
            std::copy(encoded[index].hand.begin(), encoded[index].hand.end(),
                      hands.begin() + index * HandSize);
            std::copy(encoded[index].meta.begin(), encoded[index].meta.end(),
                      metas.begin() + index * MetaSize);
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
        if (outputs[0].GetTensorTypeAndShapeInfo().GetElementCount() !=
                batch * MSHOGI_ACTION_COUNT ||
            outputs[1].GetTensorTypeAndShapeInfo().GetElementCount() != batch) {
            throw std::runtime_error("模型输出形状不兼容");
        }
        const float* policy = outputs[0].GetTensorData<float>();
        const float* values = outputs[1].GetTensorData<float>();
        if (!std::all_of(policy, policy + batch * MSHOGI_ACTION_COUNT,
                         [](float item) { return std::isfinite(item); }) ||
            !std::all_of(values, values + batch,
                         [](float item) { return std::isfinite(item); })) {
            throw std::runtime_error("模型输出包含非有限值");
        }

        std::vector<PolicyValuePrediction> predictions(batch);
        for (std::size_t index = 0; index < batch; ++index) {
            std::copy(policy + index * MSHOGI_ACTION_COUNT,
                      policy + (index + 1) * MSHOGI_ACTION_COUNT,
                      predictions[index].policyLogits.begin());
            predictions[index].value = values[index];
        }
        return predictions;
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

    std::unique_ptr<Ort::Env> m_env;
    std::unique_ptr<Ort::SessionOptions> m_options;
    std::unique_ptr<Ort::MemoryInfo> m_memory;
    std::unique_ptr<Ort::Session> m_session;
};

OnnxEvaluator::OnnxEvaluator(std::filesystem::path runtimePath,
                             std::filesystem::path modelPath)
    : m_impl(std::make_unique<Impl>(runtimePath, modelPath)) {}

OnnxEvaluator::~OnnxEvaluator() = default;

std::vector<PolicyValuePrediction> OnnxEvaluator::evaluate(
    const std::vector<GameCore>& positions) const {
    return m_impl->evaluate(positions);
}
