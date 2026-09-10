#include <gtest/gtest.h>
#include <optional>
#include <QSignalSpy>
#include "GameEngine.h"

class GameIntegrationTest : public ::testing::Test {
protected:
    GameEngine game;

    void SetUp() override {
        game.startGame(300, 5);
    }
};

TEST_F(GameIntegrationTest, InitialLayoutAndTurnAreCanonical) {
    EXPECT_EQ(game.getCurrentState(), GameState::Playing);
    EXPECT_EQ(game.getCurrentPlayer(), Player::Sente);
    EXPECT_EQ(game.getBoard().getPiece(0, 5)->getType(), PieceType::Bishop);
    EXPECT_EQ(game.getBoard().getPiece(2, 5)->getType(), PieceType::King);
    EXPECT_EQ(game.getBoard().getPiece(4, 5)->getType(), PieceType::Rook);
    EXPECT_EQ(game.getBoard().getPiece(2, 0)->getOwner(), Player::Gote);
    EXPECT_TRUE(game.getHistory().getHistory().empty());
}

TEST_F(GameIntegrationTest, EngineRejectsMovingOutOfTurn) {
    EXPECT_FALSE(game.makeMove(
        Move::makeMove(2, 1, 2, 2, Player::Gote)));
    EXPECT_EQ(game.getCurrentPlayer(), Player::Sente);
    EXPECT_TRUE(game.getHistory().getHistory().empty());
}

TEST_F(GameIntegrationTest, CaptureCooldownDropAndUndoPreserveExactHandState) {
    ASSERT_TRUE(game.makeMove(
        Move::makeMove(2, 4, 2, 3, Player::Sente)));
    ASSERT_TRUE(game.makeMove(
        Move::makeMove(2, 1, 2, 2, Player::Gote)));
    ASSERT_TRUE(game.makeMove(
        Move::makeMove(2, 3, 2, 2, Player::Sente)));

    ASSERT_EQ(game.getBoard().getHand(Player::Sente).size(), 1u);
    EXPECT_EQ(game.getHistory().peek()->notation, "先兵 - 三四 - 吃兵");
    EXPECT_EQ(game.getBoard().getHand(Player::Sente).front()->getTurnsInHand(), 2);

    ASSERT_TRUE(game.makeMove(
        Move::makeMove(0, 1, 0, 2, Player::Gote)));
    EXPECT_EQ(game.getBoard().getHand(Player::Sente).front()->getTurnsInHand(), 3);
    EXPECT_FALSE(game.makeMove(
        Move::makeDrop(1, 4, PieceType::Pawn, Player::Sente)));

    ASSERT_TRUE(game.makeMove(
        Move::makeMove(4, 5, 3, 5, Player::Sente)));
    ASSERT_TRUE(game.makeMove(
        Move::makeMove(0, 2, 0, 3, Player::Gote)));
    ASSERT_TRUE(game.makeMove(
        Move::makeDrop(1, 4, PieceType::Pawn, Player::Sente)));

    EXPECT_TRUE(game.getBoard().getHand(Player::Sente).empty());
    EXPECT_EQ(game.getHistory().peek()->notation, "先兵 - 二二打入");

    game.undo();
    EXPECT_EQ(game.getCurrentPlayer(), Player::Sente);
    EXPECT_EQ(game.getBoard().getPiece(1, 4), nullptr);
    ASSERT_EQ(game.getBoard().getHand(Player::Sente).size(), 1u);
    EXPECT_GT(game.getBoard().getHand(Player::Sente).front()->getTurnsInHand(), 3);

    game.undo();
    game.undo();
    game.undo();
    game.undo();

    EXPECT_EQ(game.getBoard().getPiece(2, 3)->getOwner(), Player::Sente);
    EXPECT_EQ(game.getBoard().getPiece(2, 2)->getOwner(), Player::Gote);
    EXPECT_TRUE(game.getBoard().getHand(Player::Sente).empty());
}

TEST_F(GameIntegrationTest, PromotionCaptureNotationAndEndUndoAreConsistent) {
    std::optional<GameEndReason> endReason;
    QObject::connect(&game, &GameEngine::gameEnded,
                     [&endReason](int, GameEndReason reason) { endReason = reason; });

    ASSERT_TRUE(game.makeMove(
        Move::makeMove(2, 4, 2, 3, Player::Sente)));
    ASSERT_TRUE(game.makeMove(
        Move::makeMove(0, 1, 0, 2, Player::Gote)));
    ASSERT_TRUE(game.makeMove(
        Move::makeMove(2, 3, 2, 2, Player::Sente)));
    ASSERT_TRUE(game.makeMove(
        Move::makeMove(0, 2, 0, 3, Player::Gote)));
    ASSERT_TRUE(game.makeMove(
        Move::makeMove(2, 2, 2, 1, Player::Sente)));
    ASSERT_TRUE(game.makeMove(
        Move::makeMove(4, 1, 4, 2, Player::Gote)));
    ASSERT_TRUE(game.makeMove(
        Move::makeMove(2, 1, 2, 0, Player::Sente)));

    ASSERT_NE(game.getBoard().getPiece(2, 0), nullptr);
    EXPECT_EQ(game.getBoard().getPiece(2, 0)->getType(), PieceType::Hou);
    EXPECT_EQ(game.getHistory().peek()->notation, "先兵 - 三六 - 吃王");
    EXPECT_EQ(game.getCurrentState(), GameState::End);
    ASSERT_TRUE(endReason.has_value());
    EXPECT_EQ(*endReason, GameEndReason::KingCaptured);

    game.undo();

    EXPECT_EQ(game.getCurrentState(), GameState::Playing);
    EXPECT_EQ(game.getCurrentPlayer(), Player::Sente);
    EXPECT_EQ(game.getBoard().getPiece(2, 1)->getType(), PieceType::Pawn);
    EXPECT_EQ(game.getBoard().getPiece(2, 0)->getType(), PieceType::King);
    EXPECT_EQ(game.getBoard().getPiece(2, 0)->getOwner(), Player::Gote);
}

TEST_F(GameIntegrationTest, UndoRestoresClockSnapshot) {
    ASSERT_EQ(game.getClock()->getSenteTime(), 300);
    ASSERT_TRUE(game.makeMove(
        Move::makeMove(2, 4, 2, 3, Player::Sente)));
    EXPECT_EQ(game.getClock()->getSenteTime(), 305);

    game.undo();

    EXPECT_EQ(game.getClock()->getSenteTime(), 300);
    EXPECT_EQ(game.getClock()->getGoteTime(), 300);
    EXPECT_EQ(game.getCurrentPlayer(), Player::Sente);
}

TEST_F(GameIntegrationTest, RestartAndResignDoNotLeaveUndoableHistory) {
    ASSERT_TRUE(game.makeMove(
        Move::makeMove(2, 4, 2, 3, Player::Sente)));
    ASSERT_TRUE(game.getHistory().canUndo());

    game.startGame(600, 10);

    EXPECT_FALSE(game.getHistory().canUndo());
    EXPECT_EQ(game.getCurrentPlayer(), Player::Sente);
    EXPECT_EQ(game.getClock()->getSenteTime(), 600);

    ASSERT_TRUE(game.makeMove(
        Move::makeMove(2, 4, 2, 3, Player::Sente)));
    std::optional<GameEndReason> endReason;
    QObject::connect(&game, &GameEngine::gameEnded,
                     [&endReason](int, GameEndReason reason) { endReason = reason; });
    game.resign();

    EXPECT_EQ(game.getCurrentState(), GameState::End);
    ASSERT_TRUE(endReason.has_value());
    EXPECT_EQ(*endReason, GameEndReason::Resignation);
    EXPECT_FALSE(game.getHistory().canUndo());
    game.undo();
    EXPECT_EQ(game.getCurrentState(), GameState::End);
}

TEST(GameEngineAgentTest, DeployedAgentMovesWithoutBlockingTheUiThread) {
    GameEngine game;
    game.setAgent(Player::Sente, std::make_shared<AlphaBetaAgent>(3));
    QSignalSpy moveSpy(&game, &GameEngine::moveExecuted);

    game.startGame(300, 0);

    ASSERT_TRUE(moveSpy.wait(3000));
    EXPECT_EQ(game.getHistory().getHistory().size(), 1u);
    EXPECT_EQ(game.getCurrentPlayer(), Player::Gote);
    EXPECT_FALSE(game.isAgentTurn());
}

TEST(GameEngineAgentTest, UndoReturnsToTheHumanDecisionPoint) {
    GameEngine game;
    game.setAgent(Player::Gote, std::make_shared<AlphaBetaAgent>(1));
    QSignalSpy moveSpy(&game, &GameEngine::moveExecuted);
    game.startGame(300, 0);

    ASSERT_TRUE(game.makeMove(
        Move::makeMove(2, 4, 2, 3, Player::Sente)));
    ASSERT_TRUE(moveSpy.wait(3000));
    ASSERT_EQ(game.getHistory().getHistory().size(), 2u);

    game.undo();

    EXPECT_TRUE(game.getHistory().getHistory().empty());
    EXPECT_EQ(game.getCurrentPlayer(), Player::Sente);
    EXPECT_NE(game.getBoard().getPiece(2, 4), nullptr);
    EXPECT_EQ(game.getBoard().getPiece(2, 3), nullptr);
}
