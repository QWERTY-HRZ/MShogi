#include <gtest/gtest.h>
#include "GameAgent.h"

TEST(GameAgentTest, BaselineReturnsALegalInitialAction) {
    GameCore core;
    AlphaBetaAgent agent(3);
    const auto selected = agent.chooseAction(core);
    ASSERT_TRUE(selected.has_value());

    GameCore child = core.fork();
    EXPECT_TRUE(child.applyAction(*selected));
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
