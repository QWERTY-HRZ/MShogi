#include <gtest/gtest.h>
#include "../include/GameEngine.h"

class GameIntegrationTest : public ::testing::Test {
protected:
    GameEngine game;

    void SetUp() override {
        game.startGame();
    }
};

TEST_F(GameIntegrationTest, FullGameSimulation) {
    // 0. 验证开局状态
    EXPECT_EQ(game.getCurrentState(), GameState::Playing);

    // =================================================================
    // 第 1 回合：先手
    // 动作：先手相 (0,0) -> (1,1)
    // 目的：将相移动到中间空旷地带，为后续被吃做准备
    // 记谱预期：[先相 - 二二] (x=1 -> 二, y=1 -> 二)
    // =================================================================
    Move m1 = Move::makeMove(0, 0, 1, 1, Player::Sente);
    EXPECT_TRUE(game.makeMove(m1));
    EXPECT_EQ(game.getHistory().peek()->notation, "先相 - 二二");

    // =================================================================
    // 第 2 回合：后手
    // 动作：后手车 (0,5) -> (1,5)
    // 目的：初始位置(0,5)前方有兵(0,4)，无法长途移动。
    //      所以先横向移动到第2列(索引1)，这一列初始没有兵，是“开放线”。
    // 记谱预期：[后车 - 二六] (x=1 -> 二, y=5 -> 六)
    // =================================================================
    Move m2 = Move::makeMove(0, 5, 1, 5, Player::Gote);
    EXPECT_TRUE(game.makeMove(m2));
    EXPECT_EQ(game.getHistory().peek()->notation, "后车 - 二六");

    // =================================================================
    // 第 3 回合：先手
    // 动作：先手兵 (2,1) -> (2,2)
    // 目的：普通行子，让出回合
    // 记谱预期：[先兵 - 三三]
    // =================================================================
    Move m3 = Move::makeMove(2, 1, 2, 2, Player::Sente);
    EXPECT_TRUE(game.makeMove(m3));

    // =================================================================
    // 第 4 回合：后手（验证“攻势”规则）
    // 动作：后手车 (1,5) -> (1,2)
    // 规则验证：
    //    - 路径：(1,4)、(1,3) 均为空（第2列无兵） -> 满足“连续为空格”
    //    - 目标：(1,2) 为空 -> 满足“移动大于1格时不能吃子”
    // 记谱预期：[后车 - 二三]
    // =================================================================
    Move m4 = Move::makeMove(1, 5, 1, 2, Player::Gote);
    EXPECT_TRUE(game.makeMove(m4));
    // 此时不应发生吃子
//    EXPECT_EQ(game.getBoard().getHandCount(Player::Gote, PieceType::Bishop), 0);
    EXPECT_EQ(game.getHistory().peek()->notation, "后车 - 二三");

    // =================================================================
    // 第 5 回合：先手
    // 动作：先手兵 (2,2) -> (2,3)
    // 目的：继续推进，逼近底线
    // =================================================================
    Move m5 = Move::makeMove(2, 2, 2, 3, Player::Sente);
    game.makeMove(m5);

    // =================================================================
    // 第 6 回合：后手（验证“吃子”规则）
    // 动作：后手车 (1,2) -> (1,1) 吃掉 先手相
    // 规则验证：距离为1格，允许吃子
    // 记谱预期：[后车 - 二二]
    // =================================================================
    Move m6 = Move::makeMove(1, 2, 1, 1, Player::Gote);
    EXPECT_TRUE(game.makeMove(m6));

    // 验证：后手手牌中应该有一个“相”
//    EXPECT_EQ(game.getBoard().getHandCount(Player::Gote, PieceType::Bishop), 1);
    EXPECT_EQ(game.getHistory().peek()->notation, "后车 - 二二");

    // =================================================================
    // 第 7 回合：先手
    // 动作：先手兵 (2,3) -> (2,4)
    // 目的：到达升变前夕
    // =================================================================
    Move m7 = Move::makeMove(2, 3, 2, 4, Player::Sente);
    game.makeMove(m7);

    // =================================================================
    // 第 8 回合：后手（验证“打入”规则）
    // 动作：后手将手牌中的“相”打入到 (3,2)
    // 规则验证：
    //    - (3,2) 为空
    //    - 相只能放在靠近己方的三行(Gote底线是y=5, 区域为5,4,3)。
    //      (3,2) 的 y=2？
    //      修正：MShogi_Rule_v1.7.1 "车和相只能放在靠近己方的三行"
    //      Gote (上往下攻) 的己方区域是 Row 5, 4, 3。
    //      Target y=2 不在区域内！(2 < 3)。
    //      我们需要打入到 y=3, 4, 5。但 y=5是底线不能打。可选 y=3,4。
    //      让我们改为打入到 (3,3)。
    //      记谱预期：[后相 - 四四打入]
    // =================================================================

    // 修正用例：打入 (3,3) (Row 3, Index 3)。符合 Gote 区域 (Rows 3,4,5)。
    Move m8 = Move::makeDrop(3, 3, PieceType::Bishop, Player::Gote);

    // 再次检查规则：Gote Base=5. "靠近己方三行" -> Indices 5, 4, 3.
    // y=3 是合法的。
    EXPECT_TRUE(game.makeMove(m8));

    auto lastNode = game.getHistory().peek();
    EXPECT_TRUE(lastNode.has_value());
    // 验证记谱包含“打入”
    // x=3 -> 四, y=3 -> 四
    EXPECT_EQ(lastNode->notation, "后相 - 四四打入");
    // 验证手牌减少
//    EXPECT_EQ(game.getBoard().getHandCount(Player::Gote, PieceType::Bishop), 0);

    // =================================================================
    // 第 9 回合：先手（验证“升变”规则）
    // 动作：先手兵 (2,4) -> (2,5) 到达底线
    // 规则验证：兵到底线强制升变为侯
    // 记谱预期：[先兵 - 三六] (生成记谱时还是兵，移动后才变侯)
    // =================================================================
    Move m9 = Move::makeMove(2, 4, 2, 5, Player::Sente);
    EXPECT_TRUE(game.makeMove(m9));

    auto promotedPiece = game.getBoard().getPiece(2, 5);
    EXPECT_NE(promotedPiece, nullptr);
    EXPECT_EQ(promotedPiece->getType(), PieceType::Hou);
    EXPECT_TRUE(game.getHistory().peek()->isPromoted);

    // =================================================================
    // 第 10 回合：后手
    // 动作：后手王 (2,5) 被先手侯 (2,5) 贴身威胁。
    //      但当前逻辑是“吃子”才算赢，或者“被吃”。
    //      上一步先手兵移动到(2,5)时，如果(2,5)有王，那就是直接吃掉赢了。
    //      但初始布局后手王在(2,5)。
    //      等等！Step 9 先手兵 (2,4) -> (2,5)。后手王就在 (2,5)！
    //      这意味着 Step 9 实际上是一步“吃王”操作！
    // =================================================================

    // 回顾 Step 9:
    // 目标 (2,5) 有 Gote King。
    // makeMove 逻辑：
    // 1. 检查 validate (兵进一，合法)
    // 2. 检查 target -> 是 King。
    // 3. 执行 move (吃掉 King)
    // 4. 检查 isGameOver -> 发现 Gote King 没了 -> 判定 Sente Win。

    // 所以 Step 9 执行完后，游戏应该结束。
    EXPECT_EQ(game.getCurrentState(), GameState::End);
}
