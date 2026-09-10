#include <gtest/gtest.h>
#include "GameCore.h"

TEST(GameCoreTest, InitialStateIsHeadlessAndHasLegalActions) {
    GameCore core;
    EXPECT_EQ(core.currentPlayer(), Player::Sente);
    EXPECT_FALSE(core.isTerminal());
    EXPECT_FALSE(core.legalActions().empty());
    EXPECT_NE(core.serializeState().find("v1.10.0|turn=S|board="), std::string::npos);
    EXPECT_FALSE(core.isLegalAction(
        Move::makeMove(2, 4, GameConstants::COLS, 3, Player::Sente)));
}

TEST(GameCoreTest, ApplyUndoAndSerializationAreExact) {
    GameCore core;
    const std::string initialState = core.serializeState();
    CoreMoveResult result;

    ASSERT_TRUE(core.applyAction(
        Move::makeMove(2, 4, 2, 3, Player::Sente), &result));
    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(core.currentPlayer(), Player::Gote);
    EXPECT_EQ(core.plyCount(), 1u);
    EXPECT_NE(core.serializeState(), initialState);

    ASSERT_TRUE(core.undoAction());
    EXPECT_EQ(core.currentPlayer(), Player::Sente);
    EXPECT_EQ(core.plyCount(), 0u);
    EXPECT_EQ(core.serializeState(), initialState);
}

TEST(GameCoreTest, ForkHasIndependentPiecesAndHistory) {
    GameCore original;
    ASSERT_TRUE(original.applyAction(
        Move::makeMove(2, 4, 2, 3, Player::Sente)));
    GameCore branch = original.fork();

    ASSERT_TRUE(branch.applyAction(
        Move::makeMove(2, 1, 2, 2, Player::Gote)));
    EXPECT_NE(branch.serializeState(), original.serializeState());
    EXPECT_EQ(branch.plyCount(), 1u);
    EXPECT_EQ(original.plyCount(), 1u);
    EXPECT_NE(original.board().getPiece(2, 1), nullptr);
    EXPECT_EQ(original.board().getPiece(2, 2), nullptr);
}

TEST(GameCoreTest, ReportsPromotionAndKingCapture) {
    GameCore core;
    ASSERT_TRUE(core.applyAction(Move::makeMove(2, 4, 2, 3, Player::Sente)));
    ASSERT_TRUE(core.applyAction(Move::makeMove(0, 1, 0, 2, Player::Gote)));
    ASSERT_TRUE(core.applyAction(Move::makeMove(2, 3, 2, 2, Player::Sente)));
    ASSERT_TRUE(core.applyAction(Move::makeMove(0, 2, 0, 3, Player::Gote)));
    ASSERT_TRUE(core.applyAction(Move::makeMove(2, 2, 2, 1, Player::Sente)));
    ASSERT_TRUE(core.applyAction(Move::makeMove(4, 1, 4, 2, Player::Gote)));

    CoreMoveResult result;
    ASSERT_TRUE(core.applyAction(
        Move::makeMove(2, 1, 2, 0, Player::Sente), &result));
    EXPECT_TRUE(result.promoted);
    ASSERT_TRUE(result.capturedType.has_value());
    EXPECT_EQ(*result.capturedType, PieceType::King);
    EXPECT_TRUE(result.terminal);
    EXPECT_EQ(result.winner, 1);
    EXPECT_EQ(result.endReason, CoreEndReason::KingCaptured);
    EXPECT_TRUE(core.legalActions().empty());
}
