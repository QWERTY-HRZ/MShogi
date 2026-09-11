#include <gtest/gtest.h>
#include "Board.h"

class BoardTest : public ::testing::Test {
protected:
    Board board;
};

TEST_F(BoardTest, BoundsAndBottomLinesUseSharedConstants) {
    EXPECT_TRUE(board.isInside(0, 0));
    EXPECT_TRUE(board.isInside(GameConstants::COLS - 1, GameConstants::ROWS - 1));
    EXPECT_FALSE(board.isInside(GameConstants::COLS, 0));
    EXPECT_FALSE(board.isInside(0, GameConstants::ROWS));
    EXPECT_EQ(board.getBottomLine(Player::Sente), GameConstants::GOTE_BASE_Y);
    EXPECT_EQ(board.getBottomLine(Player::Gote), GameConstants::SENTE_BASE_Y);
}

TEST_F(BoardTest, PlacementRejectsNullAndOccupiedSquares) {
    auto king = std::make_shared<King>(Player::Sente);
    auto pawn = std::make_shared<Pawn>(Player::Gote);

    EXPECT_TRUE(board.placePiece(2, 2, king));
    EXPECT_FALSE(board.placePiece(2, 2, pawn));
    EXPECT_FALSE(board.placePiece(1, 1, nullptr));
    EXPECT_EQ(board.getPiece(2, 2), king);
}

TEST_F(BoardTest, MoveAndRemovePreservePieceIdentity) {
    auto bishop = std::make_shared<Bishop>(Player::Gote);
    auto pawn = std::make_shared<Pawn>(Player::Sente);
    ASSERT_TRUE(board.placePiece(3, 3, bishop));
    ASSERT_TRUE(board.placePiece(2, 2, pawn));

    EXPECT_TRUE(board.movePiece(3, 3, 2, 2));
    EXPECT_EQ(board.getPiece(2, 2), bishop);
    EXPECT_EQ(board.removePiece(2, 2), bishop);
    EXPECT_EQ(board.getPiece(2, 2), nullptr);
    EXPECT_FALSE(board.movePiece(3, 3, 3, 3));
}

TEST_F(BoardTest, HandSelectionUsesEligiblePieceIdentity) {
    auto forbiddenPawn = std::make_shared<Pawn>(Player::Sente);
    forbiddenPawn->setTurnsInHand(3);
    auto availablePawn = std::make_shared<Pawn>(Player::Sente);

    board.addToHand(forbiddenPawn);
    board.addToHand(availablePawn);

    EXPECT_EQ(availablePawn->getTurnsInHand(), Piece::HAND_READY_TURNS);
    EXPECT_TRUE(board.hasDroppablePiece(Player::Sente, PieceType::Pawn));
    EXPECT_EQ(board.takeFromHand(Player::Sente, PieceType::Pawn), availablePawn);
    EXPECT_FALSE(board.hasDroppablePiece(Player::Sente, PieceType::Pawn));
    ASSERT_EQ(board.getHand(Player::Sente).size(), 1u);
    EXPECT_EQ(board.getHand(Player::Sente).front(), forbiddenPawn);
}

TEST_F(BoardTest, HandTurnUpdatesStopAtCanonicalReadyState) {
    auto pawn = std::make_shared<Pawn>(Player::Gote);
    pawn->setTurnsInHand(2);
    board.addToHand(pawn);

    board.advanceHandTurns();
    EXPECT_EQ(pawn->getTurnsInHand(), 3);
    board.advanceHandTurns();
    EXPECT_EQ(pawn->getTurnsInHand(), Piece::HAND_READY_TURNS);
    board.advanceHandTurns();
    EXPECT_EQ(pawn->getTurnsInHand(), Piece::HAND_READY_TURNS);

    pawn->setTurnsInHand(100);
    EXPECT_EQ(pawn->getTurnsInHand(), Piece::HAND_READY_TURNS);
    pawn->setTurnsInHand(-1);
    EXPECT_EQ(pawn->getTurnsInHand(), 0);
}
