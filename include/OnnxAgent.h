#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include "GameAgent.h"

class OnnxAgent final : public GameAgent {
public:
    OnnxAgent(std::filesystem::path runtimePath,
              std::filesystem::path modelPath,
              int topK = 5,
              float policyWeight = 1.0f,
              float valueWeight = 0.5f);
    ~OnnxAgent() override;

    std::string name() const override { return "PolicyValue-20k-Top5"; }
    std::optional<Move> chooseAction(const GameCore& core) const override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
    AlphaBetaAgent m_fallback{2};
};
