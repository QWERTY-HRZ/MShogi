#include <gtest/gtest.h>
#include <QApplication>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QTimer>
#include <QDebug>
#include "../include/UIController.h"
#include "../include/GameScene.h"
#include "../include/PieceItem.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

class UITest : public ::testing::Test {
protected:
    UIController* window = nullptr;
    GameScene* scene = nullptr;
    QGraphicsView* view = nullptr;

    void SetUp() override {
        window = new UIController();
        window->resize(1200, 1000);
        window->show();

        view = window->findChild<QGraphicsView*>();
        ASSERT_TRUE(view != nullptr);
        view->resetTransform();
        view->centerOn(250, 400);

        scene = qobject_cast<GameScene*>(view->scene());
        ASSERT_TRUE(scene != nullptr);

        QApplication::processEvents();
    }

    void TearDown() override {
        delete window;
        window = nullptr;
    }

    void simulateDrag(QGraphicsView* viewPtr, QPoint start, QPoint end) {
        if (!viewPtr) return;
        QMouseEvent pressEvent(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewPtr->viewport(), &pressEvent);
        QApplication::processEvents();

        for (int i = 1; i <= 10; ++i) {
            QPoint mid = start + (end - start) * (i / 10.0);
            QMouseEvent moveEvent(QEvent::MouseMove, mid, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(viewPtr->viewport(), &moveEvent);
            QApplication::processEvents();
        }

        QMouseEvent releaseEvent(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewPtr->viewport(), &releaseEvent);
        QApplication::processEvents();
    }

    // 辅助函数：检查棋子位置
    bool checkPieceAt(int gridX, int gridY, PieceType type, Player owner) {
        QList<QGraphicsItem*> items = scene->items();
        for (auto item : items) {
            PieceItem* p = dynamic_cast<PieceItem*>(item);
            if (p && p->getGridX() == gridX && p->getGridY() == gridY &&
                p->getType() == type && p->getOwner() == owner) {
                return true;
            }
        }
        return false;
    }
};

// 用例 1: 合法拖拽 (先手兵前进)
TEST_F(UITest, TestValidDrag) {
    QApplication::processEvents();
    if (!view || !scene) return;

    // 目标：Sente Pawn (2,1) -> (2,2)
    int offX = GameScene::BOARD_OFFSET_X, offY = GameScene::BOARD_OFFSET_Y;
    int cellSize = GameScene::CELL_SIZE;

    // 起点 (2,1)
    QPointF startScene = QPointF(offX + 2*cellSize + cellSize/2, offY + (5-1)*cellSize + cellSize/2);
    // 终点 (2,2)
    QPointF endScene = QPointF(offX + 2*cellSize + cellSize/2, offY + (5-2)*cellSize + cellSize/2);

    simulateDrag(view, view->mapFromScene(startScene), view->mapFromScene(endScene));
    QApplication::processEvents();

    EXPECT_TRUE(checkPieceAt(2, 2, PieceType::Pawn, Player::Sente)) << "Valid move failed";
}

// 用例 2: 非法规则拖拽 (先手兵后退)
TEST_F(UITest, TestInvalidRuleDrag) {
    QApplication::processEvents();
    if (!view || !scene) return;

    // 目标：Sente Pawn (2,1) -> (2,0) [后退，非法]
    // 为了确保测试有效，先确认 (2,0) 是空的? 不，(2,0) 是 Sente King。
    // 那我们试着横移 (2,1) -> (3,1)。兵不能横移。

    int offX = GameScene::BOARD_OFFSET_X, offY = GameScene::BOARD_OFFSET_Y;
    int cellSize = GameScene::CELL_SIZE;

    // 起点 (2,1)
    QPointF startScene = QPointF(offX + 2*cellSize + cellSize/2, offY + (5-1)*cellSize + cellSize/2);
    // 终点 (3,1) (右移一格)
    QPointF endScene = QPointF(offX + 3*cellSize + cellSize/2, offY + (5-1)*cellSize + cellSize/2);

    simulateDrag(view, view->mapFromScene(startScene), view->mapFromScene(endScene));
    QApplication::processEvents();

    // 断言：移动失败，棋子应该还在原位 (2,1)，或者被刷新回原位
    // 且 (3,1) 不应该有该棋子
    EXPECT_TRUE(checkPieceAt(2, 1, PieceType::Pawn, Player::Sente)) << "Piece should remain at start";
    EXPECT_FALSE(checkPieceAt(3, 1, PieceType::Pawn, Player::Sente)) << "Piece invalid move succeeded";
}

// 用例 3: 非法轮次拖拽 (先手回合拖动后手棋子)
TEST_F(UITest, TestInvalidTurnDrag) {
    QApplication::processEvents();
    if (!view || !scene) return;

    // 目标：Gote Pawn (2,4) -> (2,3) [前进，路径合法，但轮次非法]
    // 游戏开始默认先手回合

    int offX = GameScene::BOARD_OFFSET_X, offY = GameScene::BOARD_OFFSET_Y;
    int cellSize = GameScene::CELL_SIZE;

    // 起点 (2,4)
    QPointF startScene = QPointF(offX + 2*cellSize + cellSize/2, offY + (5-4)*cellSize + cellSize/2);
    // 终点 (2,3)
    QPointF endScene = QPointF(offX + 2*cellSize + cellSize/2, offY + (5-3)*cellSize + cellSize/2);

    simulateDrag(view, view->mapFromScene(startScene), view->mapFromScene(endScene));
    QApplication::processEvents();

    // 断言：移动失败，后手兵还在 (2,4)
    EXPECT_TRUE(checkPieceAt(2, 4, PieceType::Pawn, Player::Gote)) << "Opponent piece moved during wrong turn";
}
