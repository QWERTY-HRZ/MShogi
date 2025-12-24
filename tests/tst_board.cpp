#include <gtest/gtest.h>
#include "../include/Board.h"
#include "../include/Piece.h"

class BoardTest : public ::testing::Test {
protected:
    Board board;

    void SetUp() override {
        board.clear();
    }
};

TEST_F(BoardTest, IsInsideCheck) {
    EXPECT_TRUE(board.isInside(0, 0));
    EXPECT_TRUE(board.isInside(4, 5));
    EXPECT_FALSE(board.isInside(5, 5));
    EXPECT_FALSE(board.isInside(4, 6));
    EXPECT_FALSE(board.isInside(-1, 0));
}

TEST_F(BoardTest, PlaceAndGetPiece) {
    auto king = std::make_shared<King>(Player::Sente);
    EXPECT_TRUE(board.placePiece(2, 0, king));

    auto retrieved = board.getPiece(2, 0);
    EXPECT_EQ(retrieved, king);
    EXPECT_EQ(retrieved->getType(), PieceType::King);
    EXPECT_EQ(retrieved->getOwner(), Player::Sente);
}

TEST_F(BoardTest, PlacePieceOutOfBounds) {
    auto pawn = std::make_shared<Pawn>(Player::Gote);
    EXPECT_FALSE(board.placePiece(10, 10, pawn));
}

TEST_F(BoardTest, RemovePiece) {
    auto rook = std::make_shared<Rook>(Player::Sente);
    board.placePiece(1, 1, rook);

    auto removed = board.removePiece(1, 1);
    EXPECT_EQ(removed, rook);
    EXPECT_EQ(board.getPiece(1, 1), nullptr);
}

TEST_F(BoardTest, MovePieceValid) {
    auto bishop = std::make_shared<Bishop>(Player::Gote);
    board.placePiece(3, 3, bishop);

    bool result = board.movePiece(3, 3, 2, 2);

    EXPECT_TRUE(result);
    EXPECT_EQ(board.getPiece(3, 3), nullptr);
    EXPECT_EQ(board.getPiece(2, 2), bishop);
}

TEST_F(BoardTest, MovePieceInvalidSource) {
    EXPECT_FALSE(board.movePiece(0, 0, 1, 1));
}

TEST_F(BoardTest, MovePieceOutOfBounds) {
    auto pawn = std::make_shared<Pawn>(Player::Sente);
    board.placePiece(0, 0, pawn);

    EXPECT_FALSE(board.movePiece(0, 0, -1, 0));
    EXPECT_EQ(board.getPiece(0, 0), pawn);
}

TEST_F(BoardTest, MovePieceToSameLocation) {
    auto king = std::make_shared<King>(Player::Sente);
    board.placePiece(2, 2, king);
    EXPECT_FALSE(board.movePiece(2, 2, 2, 2));
    EXPECT_NE(board.getPiece(2, 2), nullptr); // 确保棋子不消失
}

TEST_F(BoardTest, OverwritePieceOnMove) {
    auto attacker = std::make_shared<Rook>(Player::Sente);
    auto victim = std::make_shared<Pawn>(Player::Gote);

    board.placePiece(1, 1, attacker);
    board.placePiece(1, 4, victim);

    EXPECT_TRUE(board.movePiece(1, 1, 1, 4));

    EXPECT_EQ(board.getPiece(1, 4), attacker);
    EXPECT_EQ(board.getPiece(1, 1), nullptr);
}
