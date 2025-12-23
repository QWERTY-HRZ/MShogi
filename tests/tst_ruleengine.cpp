#include <gtest/gtest.h>
#include "../include/RuleEngine.h"
#include "../include/Board.h"
#include "../include/Piece.h"

class RuleEngineTest : public ::testing::Test {
protected:
    Board board;
    RuleEngine engine;

    void SetUp() override {
        board.clear();
    }
};

// --- 基础移动测试 ---

TEST_F(RuleEngineTest, PawnMove) {
    auto pawn = std::make_shared<Pawn>(Player::Sente);
    board.placePiece(2, 2, pawn);
    
    // 合法：向前1格
    EXPECT_TRUE(engine.validateMove(board, Move::makeMove(2, 2, 2, 3, Player::Sente)));
    // 非法：向后/横向
    EXPECT_FALSE(engine.validateMove(board, Move::makeMove(2, 2, 2, 1, Player::Sente)));
    EXPECT_FALSE(engine.validateMove(board, Move::makeMove(2, 2, 3, 2, Player::Sente)));
}

TEST_F(RuleEngineTest, KingMove) {
    auto king = std::make_shared<King>(Player::Gote);
    board.placePiece(2, 2, king);
    
    // 合法：八方1格
    EXPECT_TRUE(engine.validateMove(board, Move::makeMove(2, 2, 3, 3, Player::Gote))); // 右下
    EXPECT_TRUE(engine.validateMove(board, Move::makeMove(2, 2, 2, 1, Player::Gote))); // 前(Gote视角)
    // 非法：走2格
    EXPECT_FALSE(engine.validateMove(board, Move::makeMove(2, 2, 2, 4, Player::Gote)));
}

// --- 特殊规则：攻势 (车) ---

TEST_F(RuleEngineTest, RookOffense) {
    auto rook = std::make_shared<Rook>(Player::Sente);
    board.placePiece(0, 0, rook);
    
    // 1. 基础移动 (1格)
    EXPECT_TRUE(engine.validateMove(board, Move::makeMove(0, 0, 0, 1, Player::Sente)));
    
    // 2. 攻势 (长距离滑行到空位)
    EXPECT_TRUE(engine.validateMove(board, Move::makeMove(0, 0, 0, 4, Player::Sente)));
    
    // 3. 攻势限制 (不能长距离吃子)
    auto enemy = std::make_shared<Pawn>(Player::Gote);
    board.placePiece(0, 4, enemy);
    // 试图移动4格吃子 -> 应该失败
    EXPECT_FALSE(engine.validateMove(board, Move::makeMove(0, 0, 0, 4, Player::Sente)));
    // 移动1格吃子 -> 应该成功
    board.placePiece(0, 1, enemy);
    EXPECT_TRUE(engine.validateMove(board, Move::makeMove(0, 0, 0, 1, Player::Sente)));
}

// --- 特殊规则：守略 (相) ---

TEST_F(RuleEngineTest, BishopDefense) {
    auto bishop = std::make_shared<Bishop>(Player::Sente);
    board.placePiece(0, 0, bishop);
    
    // 1. 基础移动 (斜1格)
    EXPECT_TRUE(engine.validateMove(board, Move::makeMove(0, 0, 1, 1, Player::Sente)));
    // 相不能走直格
    EXPECT_FALSE(engine.validateMove(board, Move::makeMove(0, 0, 0, 1, Player::Sente)));
    
    // 2. 守略 (隔己方子吃敌方)
    auto friendP = std::make_shared<Pawn>(Player::Sente);
    auto enemyP = std::make_shared<Pawn>(Player::Gote);
    
    board.placePiece(1, 1, friendP); // 中间有友军
    board.placePiece(2, 2, enemyP);  // 目标有敌军
    
    // 触发守略
    EXPECT_TRUE(engine.validateMove(board, Move::makeMove(0, 0, 2, 2, Player::Sente)));
    
    // 负面测试：若目标为空，不能跳
    board.removePiece(2, 2);
    EXPECT_FALSE(engine.validateMove(board, Move::makeMove(0, 0, 2, 2, Player::Sente)));
}

// --- 升变与侯 ---

TEST_F(RuleEngineTest, PromotionAndHou) {
    auto pawn = std::make_shared<Pawn>(Player::Sente);
    board.placePiece(2, 4, pawn);
    
    Move m = Move::makeMove(2, 4, 2, 5, Player::Sente); // 走到底线
    EXPECT_TRUE(engine.checkPromotion(board, m));
    
    Move m2 = Move::makeMove(2, 4, 2, 3, Player::Gote); // 没到底线 (假设反向走)
    EXPECT_FALSE(engine.checkPromotion(board, m2));

    // 测试侯的移动 (类似金将)
    auto hou = std::make_shared<Hou>(Player::Sente);
    board.placePiece(2, 2, hou);
    EXPECT_TRUE(engine.validateMove(board, Move::makeMove(2, 2, 3, 3, Player::Sente))); // 右前
    EXPECT_FALSE(engine.validateMove(board, Move::makeMove(2, 2, 3, 1, Player::Sente))); // 右后 (金将不能走)
}

// --- 打入规则 ---

TEST_F(RuleEngineTest, DropRules) {
    // 1. 基础打入
    EXPECT_TRUE(engine.validateMove(board, Move::makeDrop(2, 2, PieceType::Pawn, Player::Sente)));
    
    // 2. 不能打入底线 (Sente底线是5)
    EXPECT_FALSE(engine.validateMove(board, Move::makeDrop(2, 5, PieceType::Pawn, Player::Sente)));
    
    // 3. 车/相 区域限制 (Sente只能在 0-2)
    EXPECT_TRUE(engine.validateMove(board, Move::makeDrop(2, 2, PieceType::Rook, Player::Sente)));
    EXPECT_FALSE(engine.validateMove(board, Move::makeDrop(2, 3, PieceType::Rook, Player::Sente)));
    
    // 4. 禁手 (上一回合刚被吃的子)
    EXPECT_FALSE(engine.validateMove(board, 
        Move::makeDrop(2, 2, PieceType::Pawn, Player::Sente), 
        PieceType::Pawn // lastCaptured
    ));
}

// --- 胜负判定 ---

TEST_F(RuleEngineTest, GameOver) {
    auto sKing = std::make_shared<King>(Player::Sente);
    auto gKing = std::make_shared<King>(Player::Gote);
    
    board.placePiece(2, 0, sKing);
    board.placePiece(2, 5, gKing);
    EXPECT_EQ(engine.isGameOver(board), 0); // 都在
    
    board.removePiece(2, 0); // 先手王被吃
    EXPECT_EQ(engine.isGameOver(board), 2); // 后手胜
    
    // 下底测试
    board.placePiece(2, 0, sKing); // 恢复
    board.movePiece(2, 0, 2, 5);   // Sente王移动到底线(Row 5)
    // 此时 Sente王在 (2,5), Gote王也在 (2,5) -> 覆盖吃掉Gote王
    // 这种情况优先判吃王胜，还是下底胜？
    // isGameOver 逻辑：
    // 1. Gote王没了 -> 先手胜
    // 2. Sente王在底线 -> 先手胜
    EXPECT_EQ(engine.isGameOver(board), 1);
}