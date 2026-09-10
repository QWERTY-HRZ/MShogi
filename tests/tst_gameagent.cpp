#include <gtest/gtest.h>
#include "GameAgent.h"

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
