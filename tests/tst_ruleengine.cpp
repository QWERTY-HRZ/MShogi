#include <gtest/gtest.h>
#include "RuleEngine.h"

class RuleEngineTest : public ::testing::Test {
protected:
    Board board;
    RuleEngine engine;
};

TEST_F(RuleEngineTest, PawnsMoveTowardOpponentBaseline) {
    ASSERT_TRUE(board.placePiece(2, 3, std::make_shared<Pawn>(Player::Sente)));
    ASSERT_TRUE(board.placePiece(3, 2, std::make_shared<Pawn>(Player::Gote)));

    EXPECT_TRUE(engine.validateMove(
        board, Move::makeMove(2, 3, 2, 2, Player::Sente)));
    EXPECT_FALSE(engine.validateMove(
        board, Move::makeMove(2, 3, 2, 4, Player::Sente)));
    EXPECT_TRUE(engine.validateMove(
        board, Move::makeMove(3, 2, 3, 3, Player::Gote)));
    EXPECT_FALSE(engine.validateMove(
        board, Move::makeMove(3, 2, 3, 1, Player::Gote)));
}

TEST_F(RuleEngineTest, OwnPieceAndZeroDistanceMovesAreRejected) {
    ASSERT_TRUE(board.placePiece(2, 2, std::make_shared<King>(Player::Sente)));
    ASSERT_TRUE(board.placePiece(3, 2, std::make_shared<Pawn>(Player::Sente)));

    EXPECT_FALSE(engine.validateMove(
        board, Move::makeMove(2, 2, 2, 2, Player::Sente)));
    EXPECT_FALSE(engine.validateMove(
        board, Move::makeMove(2, 2, 3, 2, Player::Sente)));
    EXPECT_FALSE(engine.validateMove(
        board, Move::makeMove(2, 2, 1, 2, Player::Gote)));
}

TEST_F(RuleEngineTest, RookSlidesOnlyThroughEmptySquaresAndCapturesAtOneStep) {
    ASSERT_TRUE(board.placePiece(0, 4, std::make_shared<Rook>(Player::Sente)));

    EXPECT_TRUE(engine.validateMove(
        board, Move::makeMove(0, 4, 3, 4, Player::Sente)));

    ASSERT_TRUE(board.placePiece(2, 4, std::make_shared<Pawn>(Player::Gote)));
    EXPECT_FALSE(engine.validateMove(
        board, Move::makeMove(0, 4, 3, 4, Player::Sente)));
    EXPECT_FALSE(engine.validateMove(
        board, Move::makeMove(0, 4, 2, 4, Player::Sente)));

    board.removePiece(2, 4);
    ASSERT_TRUE(board.placePiece(1, 4, std::make_shared<Pawn>(Player::Gote)));
    EXPECT_TRUE(engine.validateMove(
        board, Move::makeMove(0, 4, 1, 4, Player::Sente)));
}

TEST_F(RuleEngineTest, RookAssaultCapturesOnlyTwoSquaresVertically) {
    auto rook = std::make_shared<Rook>(Player::Sente);
    ASSERT_TRUE(board.placePiece(2, 4, rook));
    ASSERT_TRUE(board.placePiece(2, 2, std::make_shared<Pawn>(Player::Gote)));

    const Move verticalAssault = Move::makeMove(2, 4, 2, 2, Player::Sente);
    EXPECT_TRUE(engine.validateMove(board, verticalAssault));
    ASSERT_TRUE(board.movePiece(2, 4, 2, 2));
    EXPECT_EQ(board.getPiece(2, 2), rook);

    board.clear();
    ASSERT_TRUE(board.placePiece(2, 4, rook));
    ASSERT_TRUE(board.placePiece(2, 3, std::make_shared<Pawn>(Player::Sente)));
    ASSERT_TRUE(board.placePiece(2, 2, std::make_shared<Pawn>(Player::Gote)));
    EXPECT_FALSE(engine.validateMove(board, verticalAssault));

    board.clear();
    ASSERT_TRUE(board.placePiece(1, 3, rook));
    ASSERT_TRUE(board.placePiece(3, 3, std::make_shared<Pawn>(Player::Gote)));
    // 横向两格即使中间为空，也不能发动突击。
    EXPECT_FALSE(engine.validateMove(
        board, Move::makeMove(1, 3, 3, 3, Player::Sente)));

    board.clear();
    ASSERT_TRUE(board.placePiece(2, 5, rook));
    ASSERT_TRUE(board.placePiece(2, 2, std::make_shared<Pawn>(Player::Gote)));
    EXPECT_FALSE(engine.validateMove(
        board, Move::makeMove(2, 5, 2, 2, Player::Sente)));
}

TEST_F(RuleEngineTest, BishopDefenseRequiresFriendlyScreenAndEnemyTarget) {
    auto bishop = std::make_shared<Bishop>(Player::Sente);
    auto screenPawn = std::make_shared<Pawn>(Player::Sente);
    ASSERT_TRUE(board.placePiece(0, 4, bishop));
    EXPECT_TRUE(engine.validateMove(
        board, Move::makeMove(0, 4, 1, 3, Player::Sente)));

    ASSERT_TRUE(board.placePiece(1, 3, screenPawn));
    ASSERT_TRUE(board.placePiece(2, 2, std::make_shared<Rook>(Player::Gote)));
    EXPECT_TRUE(engine.validateMove(
        board, Move::makeMove(0, 4, 2, 2, Player::Sente)));

    // 守略执行后相必须落在目标格，不能停在原地远程吃子。
    ASSERT_TRUE(board.movePiece(0, 4, 2, 2));
    EXPECT_EQ(board.getPiece(0, 4), nullptr);
    EXPECT_EQ(board.getPiece(1, 3), screenPawn);
    EXPECT_EQ(board.getPiece(2, 2), bishop);

    board.clear();
    ASSERT_TRUE(board.placePiece(0, 4, bishop));
    ASSERT_TRUE(board.placePiece(1, 3, screenPawn));
    EXPECT_FALSE(engine.validateMove(
        board, Move::makeMove(0, 4, 2, 2, Player::Sente)));
}

TEST_F(RuleEngineTest, PromotionAndHouDirectionsFollowBoardOrientation) {
    ASSERT_TRUE(board.placePiece(2, 1, std::make_shared<Pawn>(Player::Sente)));
    EXPECT_TRUE(engine.checkPromotion(
        board, Move::makeMove(2, 1, 2, 0, Player::Sente)));

    ASSERT_TRUE(board.placePiece(3, 4, std::make_shared<Pawn>(Player::Gote)));
    EXPECT_TRUE(engine.checkPromotion(
        board, Move::makeMove(3, 4, 3, 5, Player::Gote)));

    ASSERT_TRUE(board.placePiece(1, 3, std::make_shared<Hou>(Player::Sente)));
    EXPECT_TRUE(engine.validateMove(
        board, Move::makeMove(1, 3, 2, 2, Player::Sente)));
    EXPECT_FALSE(engine.validateMove(
        board, Move::makeMove(1, 3, 2, 4, Player::Sente)));
}

TEST_F(RuleEngineTest, DropsRequireAnEligibleOwnedHandPiece) {
    auto pawn = std::make_shared<Pawn>(Player::Sente);
    pawn->setTurnsInHand(3);
    board.addToHand(pawn);

    const Move drop = Move::makeDrop(1, 4, PieceType::Pawn, Player::Sente);
    EXPECT_FALSE(engine.validateMove(board, drop));

    pawn->setTurnsInHand(4);
    EXPECT_TRUE(engine.validateMove(board, drop));
    EXPECT_FALSE(engine.validateMove(
        board, Move::makeDrop(1, GameConstants::GOTE_BASE_Y,
                             PieceType::Pawn, Player::Sente)));

    ASSERT_TRUE(board.placePiece(1, 4, std::make_shared<Pawn>(Player::Gote)));
    EXPECT_FALSE(engine.validateMove(board, drop));
}

TEST_F(RuleEngineTest, RookAndBishopDropsStayInOwnHalf) {
    auto senteRook = std::make_shared<Rook>(Player::Sente);
    senteRook->setTurnsInHand(4);
    auto goteBishop = std::make_shared<Bishop>(Player::Gote);
    goteBishop->setTurnsInHand(4);
    board.addToHand(senteRook);
    board.addToHand(goteBishop);

    EXPECT_TRUE(engine.validateMove(
        board, Move::makeDrop(1, 3, PieceType::Rook, Player::Sente)));
    EXPECT_FALSE(engine.validateMove(
        board, Move::makeDrop(1, 2, PieceType::Rook, Player::Sente)));
    EXPECT_TRUE(engine.validateMove(
        board, Move::makeDrop(3, 2, PieceType::Bishop, Player::Gote)));
    EXPECT_FALSE(engine.validateMove(
        board, Move::makeDrop(3, 3, PieceType::Bishop, Player::Gote)));
}

TEST_F(RuleEngineTest, KingCaptureAndSurvivingOneReplyEndTheGame) {
    ASSERT_TRUE(board.placePiece(1, 1, std::make_shared<King>(Player::Sente)));
    ASSERT_TRUE(board.placePiece(3, 4, std::make_shared<King>(Player::Gote)));
    EXPECT_EQ(engine.isGameOver(board), 0);

    board.removePiece(3, 4);
    EXPECT_EQ(engine.isGameOver(board), 1);

    board.clear();
    ASSERT_TRUE(board.placePiece(1, GameConstants::GOTE_BASE_Y,
                                 std::make_shared<King>(Player::Sente)));
    ASSERT_TRUE(board.placePiece(3, 4, std::make_shared<King>(Player::Gote)));
    EXPECT_EQ(engine.isGameOver(board), 0);
    EXPECT_EQ(engine.isGameOver(board), 1);
}

TEST_F(RuleEngineTest, DetectsWhenAPlayerHasNoLegalAction) {
    // 构造完全由己方棋子占满的测试盘面，所有行子和打入目标都被封死。
    for (int x = 0; x < GameConstants::COLS; ++x) {
        for (int y = 0; y < GameConstants::ROWS; ++y) {
            ASSERT_TRUE(board.placePiece(x, y, std::make_shared<Pawn>(Player::Sente)));
        }
    }
    EXPECT_FALSE(engine.hasAnyLegalAction(board, Player::Sente));

    // 腾出一个兵的正前方后，该方立即恢复至少一种合法着法。
    board.removePiece(2, 2);
    EXPECT_TRUE(engine.hasAnyLegalAction(board, Player::Sente));
}

TEST_F(RuleEngineTest, CheckIsReportedWithoutChangingMoveLegality) {
    ASSERT_TRUE(board.placePiece(2, 2, std::make_shared<King>(Player::Sente)));
    ASSERT_TRUE(board.placePiece(2, 3, std::make_shared<Rook>(Player::Gote)));
    ASSERT_TRUE(board.placePiece(0, 4, std::make_shared<Pawn>(Player::Sente)));
    EXPECT_TRUE(engine.isKingThreatened(board, Player::Sente));
    EXPECT_TRUE(engine.validateMove(
        board, Move::makeMove(0, 4, 0, 3, Player::Sente)));

    // 车不能远距离吃子，因此拉开到三格后不再构成将军。
    board.removePiece(2, 3);
    ASSERT_TRUE(board.placePiece(2, 5, std::make_shared<Rook>(Player::Gote)));
    EXPECT_FALSE(engine.isKingThreatened(board, Player::Sente));
}
