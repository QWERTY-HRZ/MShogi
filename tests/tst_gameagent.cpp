#include <gtest/gtest.h>
#include <filesystem>
#include "GameAgent.h"
#include "SelfPlay.h"
#ifdef MSHOGI_WITH_ONNX_AGENT
#include "OnnxAgent.h"
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
