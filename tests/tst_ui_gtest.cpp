#include <gtest/gtest.h>
#include <algorithm>
#include <QApplication>
#include <QGroupBox>
#include <QGraphicsView>
#include <QLabel>
#include <QTest>
#include "UIController.h"

class UITest : public ::testing::Test {
protected:
    UIController* window = nullptr;
    GameEngine* game = nullptr;
    GameScene* scene = nullptr;
    QGraphicsView* view = nullptr;

    void SetUp() override {
        window = new UIController(nullptr, false);
        window->resize(1200, 900);
        window->show();

        game = window->findChild<GameEngine*>();
        view = window->findChild<QGraphicsView*>();
        ASSERT_NE(game, nullptr);
        ASSERT_NE(view, nullptr);

        scene = qobject_cast<GameScene*>(view->scene());
        ASSERT_NE(scene, nullptr);

        game->startGame(300, 0);
        QApplication::processEvents();
        view->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
        QApplication::processEvents();
    }

    void TearDown() override {
        delete window;
    }

    QPoint cellCenter(int x, int y) const {
        const QPointF scenePoint(
            GameScene::BOARD_OFFSET_X + x * GameScene::CELL_SIZE + GameScene::CELL_SIZE / 2.0,
            GameScene::BOARD_OFFSET_Y + y * GameScene::CELL_SIZE + GameScene::CELL_SIZE / 2.0);
        return view->mapFromScene(scenePoint);
    }

    void drag(const QPoint& start, const QPoint& end) {
        QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, start);
        for (int step = 1; step <= 5; ++step) {
            const QPoint point = start + (end - start) * step / 5;
            QTest::mouseMove(view->viewport(), point);
        }
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, end);
        QApplication::processEvents();
        QApplication::processEvents();
    }
};

TEST_F(UITest, ValidPawnDragUpdatesEngineAndScene) {
    drag(cellCenter(2, 4), cellCenter(2, 3));

    const auto piece = game->getBoard().getPiece(2, 3);
    ASSERT_NE(piece, nullptr);
    EXPECT_EQ(piece->getType(), PieceType::Pawn);
    EXPECT_EQ(piece->getOwner(), Player::Sente);
    EXPECT_EQ(game->getCurrentPlayer(), Player::Gote);
}

TEST_F(UITest, InvalidPawnDragLeavesBoardUnchanged) {
    drag(cellCenter(2, 4), cellCenter(3, 4));

    EXPECT_NE(game->getBoard().getPiece(2, 4), nullptr);
    EXPECT_EQ(game->getBoard().getPiece(3, 4), nullptr);
    EXPECT_EQ(game->getCurrentPlayer(), Player::Sente);
}

TEST_F(UITest, OpponentPieceCannotBeDragged) {
    drag(cellCenter(2, 1), cellCenter(2, 2));

    const auto piece = game->getBoard().getPiece(2, 1);
    ASSERT_NE(piece, nullptr);
    EXPECT_EQ(piece->getOwner(), Player::Gote);
    EXPECT_EQ(game->getBoard().getPiece(2, 2), nullptr);
}

TEST_F(UITest, PausedBoardCannotBeDragged) {
    game->pauseGame();
    QApplication::processEvents();

    drag(cellCenter(2, 4), cellCenter(2, 3));

    EXPECT_EQ(game->getCurrentState(), GameState::Paused);
    EXPECT_NE(game->getBoard().getPiece(2, 4), nullptr);
    EXPECT_EQ(game->getBoard().getPiece(2, 3), nullptr);
}

TEST_F(UITest, MatchInfoContainsOnlyStatusAndOpeningNumber) {
    QLabel* statusLabel = window->findChild<QLabel*>("lblStatus");
    QLabel* openingLabel = window->findChild<QLabel*>("lblOpeningDraw");
    ASSERT_NE(statusLabel, nullptr);
    ASSERT_NE(openingLabel, nullptr);

    auto* matchInfoGroup = qobject_cast<QGroupBox*>(statusLabel->parentWidget());
    ASSERT_NE(matchInfoGroup, nullptr);
    EXPECT_EQ(matchInfoGroup->title(), "对局信息");
    EXPECT_EQ(openingLabel->parentWidget(), matchInfoGroup);
    EXPECT_EQ(matchInfoGroup->findChildren<QLabel*>(
                  QString(), Qt::FindDirectChildrenOnly).size(), 2);

    const auto groups = window->findChildren<QGroupBox*>();
    const auto clockIt = std::find_if(groups.begin(), groups.end(), [](QGroupBox* group) {
        return group->title() == "棋钟";
    });
    ASSERT_NE(clockIt, groups.end());
    EXPECT_EQ((*clockIt)->findChildren<QLabel*>(
                  QString(), Qt::FindDirectChildrenOnly).size(), 3);
}
