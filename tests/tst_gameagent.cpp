#include <gtest/gtest.h>
#include <filesystem>
#include "GameAgent.h"
#include "SelfPlay.h"
#ifdef MSHOGI_WITH_ONNX_AGENT
#include "OnnxAgent.h"
#include "PuctSearch.h"
#endif

TEST(GameAgentTest, BaselineReturnsALegalInitialAction) {
    GameCore core;
    AlphaBetaAgent agent(3);
    const auto scoredActions = agent.scoreActions(core);
    EXPECT_EQ(scoredActions.size(), core.legalActions().size());
    const auto selected = agent.chooseAction(core);
    ASSERT_TRUE(selected.has_value());

    GameCore child = core.fork();
    EXPECT_TRUE(child.applyAction(*selected));
}

TEST(GameAgentTest, TemperatureSelectionIsDeterministicForASeed) {
    GameCore core;
    AlphaBetaAgent agent(2);
    const auto scoredActions = agent.scoreActions(core);
    const auto first = AlphaBetaAgent::selectAction(scoredActions, 1.0, 20260910);
    const auto second = AlphaBetaAgent::selectAction(scoredActions, 1.0, 20260910);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->fromX, second->fromX);
    EXPECT_EQ(first->fromY, second->fromY);
    EXPECT_EQ(first->toX, second->toX);
    EXPECT_EQ(first->toY, second->toY);
}

TEST(GameAgentTest, BaselineFindsImmediateKingCapture) {
    GameCore core;
    ASSERT_TRUE(core.applyAction(Move::makeMove(2, 4, 2, 3, Player::Sente)));
    ASSERT_TRUE(core.applyAction(Move::makeMove(0, 1, 0, 2, Player::Gote)));
    ASSERT_TRUE(core.applyAction(Move::makeMove(2, 3, 2, 2, Player::Sente)));
    ASSERT_TRUE(core.applyAction(Move::makeMove(0, 2, 0, 3, Player::Gote)));
    ASSERT_TRUE(core.applyAction(Move::makeMove(2, 2, 2, 1, Player::Sente)));
    ASSERT_TRUE(core.applyAction(Move::makeMove(4, 1, 4, 2, Player::Gote)));

    AlphaBetaAgent agent(2);
    const auto selected = agent.chooseAction(core);
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected->fromX, 2);
    EXPECT_EQ(selected->fromY, 1);
    EXPECT_EQ(selected->toX, 2);
    EXPECT_EQ(selected->toY, 0);
}

TEST(GameAgentTest, DrawHasNeutralValueForBothPlayers) {
    GameCore core;
    for (int cycle = 0; cycle < 2; ++cycle) {
        ASSERT_TRUE(core.applyAction(Move::makeMove(2, 5, 1, 5, Player::Sente)));
        ASSERT_TRUE(core.applyAction(Move::makeMove(2, 0, 1, 0, Player::Gote)));
        ASSERT_TRUE(core.applyAction(Move::makeMove(1, 5, 2, 5, Player::Sente)));
        ASSERT_TRUE(core.applyAction(Move::makeMove(1, 0, 2, 0, Player::Gote)));
    }

    ASSERT_TRUE(core.isTerminal());
    AlphaBetaAgent agent(1);
    EXPECT_DOUBLE_EQ(agent.evaluate(core, Player::Sente), 0.0);
    EXPECT_DOUBLE_EQ(agent.evaluate(core, Player::Gote), 0.0);
}

#ifdef MSHOGI_WITH_ONNX_AGENT
namespace {
class FakePolicyValueEvaluator final : public PolicyValueEvaluator {
public:
    std::vector<PolicyValuePrediction> evaluate(
        const std::vector<GameCore>& positions) const override {
        batchSizes.push_back(positions.size());
        std::vector<PolicyValuePrediction> predictions(positions.size());
        for (std::size_t index = 0; index < positions.size(); ++index) {
            for (const Move& move : positions[index].legalActions()) {
                predictions[index].policyLogits[encodeAction(move)] =
                    static_cast<float>(encodeAction(move)) / 990.0f;
            }
        }
        return predictions;
    }

    mutable std::vector<std::size_t> batchSizes;
};
}

TEST(GameAgentTest, PuctBatchesLeavesAndReusesSelectedTree) {
    FakePolicyValueEvaluator evaluator;
    PuctConfig config;
    config.simulations = 4;
    config.dirichletEpsilon = 0.0;
    PuctBatchSearch search(evaluator, config, 2);
    GameCore first;
    GameCore second;
    const std::vector<const GameCore*> positions{&first, &second};
    const auto results = search.search(positions, {11, 12}, {0.0, 0.0});
    ASSERT_TRUE(results[0].has_value());
    ASSERT_TRUE(results[1].has_value());
    EXPECT_TRUE(first.isLegalAction(results[0]->selectedAction));
    EXPECT_TRUE(second.isLegalAction(results[1]->selectedAction));
    EXPECT_EQ(results[0]->rootVisits, config.simulations);
    EXPECT_EQ(evaluator.batchSizes.front(), 2U);
    EXPECT_EQ(search.statistics().inferencePositions, 8U);

    const Move selected = results[0]->selectedAction;
    ASSERT_TRUE(first.applyAction(selected));
    search.advance(0, selected, first);
    const auto reused = search.search({&first, nullptr}, {13, 0}, {0.0, 0.0});
    ASSERT_TRUE(reused[0].has_value());
    EXPECT_TRUE(reused[0]->reusedTree);
    EXPECT_EQ(search.statistics().treeReuseHits, 1U);
}

TEST(GameAgentTest, PuctVirtualLossBatchesMultipleLeavesWithoutLeakingVisits) {
    FakePolicyValueEvaluator evaluator;
    PuctConfig config;
    config.simulations = 16;
    config.leavesPerBatch = 4;
    config.virtualLoss = 1.0;
    config.dirichletEpsilon = 0.0;
    PuctBatchSearch search(evaluator, config, 1);
    GameCore core;
    const auto results = search.search({&core}, {20260912}, {0.0});
    ASSERT_TRUE(results[0].has_value());
    EXPECT_EQ(results[0]->rootVisits, config.simulations);
    int childVisits = 0;
    for (const ActionVisit& action : results[0]->actionVisits) {
        EXPECT_GE(action.visits, 0);
        childVisits += action.visits;
    }
    // 根首次展开自身占一次访问，其余模拟必须全部落到子节点。
    EXPECT_EQ(childVisits + 1, results[0]->rootVisits);
    EXPECT_GE(search.statistics().maxInferenceBatch, 2U);
    EXPECT_LT(search.statistics().inferenceBatches,
              search.statistics().inferencePositions);
}

TEST(GameAgentTest, OnnxAgentLoadsDeployedModelAndReturnsLegalAction) {
    GameCore core;
    OnnxAgent agent(MSHOGI_TEST_ONNX_RUNTIME, MSHOGI_TEST_ONNX_MODEL);
    const auto selected = agent.chooseAction(core);
    ASSERT_TRUE(selected.has_value());
    EXPECT_TRUE(core.isLegalAction(*selected));
    // Python arena 对同一模型和初始局面的 top-5 重排结果固定为动作 833。
    EXPECT_EQ(encodeAction(*selected), 833);
    EXPECT_EQ(agent.name(), "PolicyValue-20k-Top5");
}

TEST(GameAgentTest, OnnxAgentRejectsMissingModelBeforeGameStarts) {
    EXPECT_THROW(OnnxAgent(MSHOGI_TEST_ONNX_RUNTIME,
                           std::filesystem::path(MSHOGI_TEST_ONNX_MODEL).parent_path() /
                               "missing.onnx"),
                 std::runtime_error);
}

TEST(GameAgentTest, OnnxAgentAlwaysTakesImmediateWinOutsidePolicyRanking) {
    GameCore core;
    ASSERT_TRUE(core.applyAction(Move::makeMove(2, 4, 2, 3, Player::Sente)));
    ASSERT_TRUE(core.applyAction(Move::makeMove(0, 1, 0, 2, Player::Gote)));
    ASSERT_TRUE(core.applyAction(Move::makeMove(2, 3, 2, 2, Player::Sente)));
    ASSERT_TRUE(core.applyAction(Move::makeMove(0, 2, 0, 3, Player::Gote)));
    ASSERT_TRUE(core.applyAction(Move::makeMove(2, 2, 2, 1, Player::Sente)));
    ASSERT_TRUE(core.applyAction(Move::makeMove(4, 1, 4, 2, Player::Gote)));

    OnnxAgent agent(MSHOGI_TEST_ONNX_RUNTIME, MSHOGI_TEST_ONNX_MODEL);
    const auto selected = agent.chooseAction(core);
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(encodeAction(*selected), encodeAction(
        Move::makeMove(2, 1, 2, 0, Player::Sente)));
}
#endif
