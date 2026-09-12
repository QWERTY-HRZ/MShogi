#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <vector>
#include "GameCore.h"

constexpr std::size_t MSHOGI_ACTION_COUNT = 990;

struct PolicyValuePrediction {
    std::array<float, MSHOGI_ACTION_COUNT> policyLogits{};
    float value = 0.0f;
};

class PolicyValueEvaluator {
public:
    virtual ~PolicyValueEvaluator() = default;
    virtual std::vector<PolicyValuePrediction> evaluate(
        const std::vector<GameCore>& positions) const = 0;
};

class OnnxEvaluator final : public PolicyValueEvaluator {
public:
    OnnxEvaluator(std::filesystem::path runtimePath,
                  std::filesystem::path modelPath);
    ~OnnxEvaluator() override;

    std::vector<PolicyValuePrediction> evaluate(
        const std::vector<GameCore>& positions) const override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
