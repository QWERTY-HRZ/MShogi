#include <gtest/gtest.h>
#include <algorithm>
#include <QApplication>
#include <QFile>
#include <QGroupBox>
#include <QGraphicsView>
#include <QLabel>
#include <QScreen>
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

class UILayoutTest : public ::testing::Test {
protected:
    QString previousStyle;

    void SetUp() override {
        Q_INIT_RESOURCE(resources);
        QFile styleFile(":/res/style.qss");
        ASSERT_TRUE(styleFile.open(QFile::ReadOnly | QFile::Text));
        previousStyle = qApp->styleSheet();
        qApp->setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    }

    void TearDown() override {
        qApp->setStyleSheet(previousStyle);
    }

    void expectSceneFits(QGraphicsView* view) {
        const QRect mappedScene = view->mapFromScene(view->scene()->sceneRect()).boundingRect();
        const QRect viewport = view->viewport()->rect();
        const QRectF sceneRect = view->scene()->sceneRect();
        // fitInView reserves a two-pixel margin on each side.
        const qreal expectedScale = qMin((viewport.width() - 4.0) / sceneRect.width(),
                                        (viewport.height() - 4.0) / sceneRect.height());
        EXPECT_NEAR(view->transform().m11(), expectedScale, 0.005);
        EXPECT_NEAR(view->transform().m11(), view->transform().m22(), 0.0001);
        EXPECT_TRUE(viewport.contains(mappedScene))
            << "Scene: " << mappedScene.x() << ',' << mappedScene.y() << ' '
            << mappedScene.width() << 'x' << mappedScene.height()
            << "; viewport: " << viewport.width() << 'x' << viewport.height();
    }
};

TEST_F(UILayoutTest, SceneFitsViewportWithoutManualResize) {
    UIController window(nullptr, false);
    window.show();
    auto* view = window.findChild<QGraphicsView*>();
    ASSERT_NE(view, nullptr);
    auto* game = window.findChild<GameEngine*>();
    ASSERT_NE(game, nullptr);
    game->startGame(300, 0);
    QTest::qWait(50);

    expectSceneFits(view);
}

TEST_F(UILayoutTest, InitialWindowFitsAvailableScreen) {
    UIController window(nullptr, false);
    window.show();
    QTest::qWait(50);
    const QSize available = window.screen()->availableGeometry().size();
    EXPECT_LE(window.frameGeometry().width(), available.width());
    EXPECT_LE(window.frameGeometry().height(), available.height());
}

TEST_F(UILayoutTest, SceneFitsAfterWindowResizes) {
    UIController window(nullptr, false);
    window.show();
    auto* view = window.findChild<QGraphicsView*>();
    ASSERT_NE(view, nullptr);
    for (const QSize size : {QSize(1200, 900), QSize(800, 600), QSize(1600, 1000)}) {
        window.resize(size);
        QTest::qWait(50);
        expectSceneFits(view);
    }
}

TEST_F(UILayoutTest, SceneFitsAfterSidebarChangesWithoutWindowResize) {
    UIController window(nullptr, false);
    window.resize(1200, 900);
    window.show();
    QTest::qWait(50);
    auto* view = window.findChild<QGraphicsView*>();
    auto* label = window.findChild<QLabel*>("lblGameInfo");
    ASSERT_NE(view, nullptr);
    ASSERT_NE(label, nullptr);
    const QSize windowSize = window.size();
    const QSize viewportSize = view->viewport()->size();
    label->setMinimumWidth(700);
    QTest::qWait(50);
    ASSERT_EQ(window.size(), windowSize);
    ASSERT_NE(view->viewport()->size(), viewportSize);
    expectSceneFits(view);
}

TEST_F(UILayoutTest, ClockWeightsSurviveMaximizeRestoreAndTimerUpdates) {
    UIController window(nullptr, false);
    window.resize(1000, 750);
    window.show();
    auto* game = window.findChild<GameEngine*>();
    auto* sente = window.findChild<QLabel*>("lblSenteTurn");
    auto* gote = window.findChild<QLabel*>("lblGoteTurn");
    auto* info = window.findChild<QLabel*>("lblGameInfo");
    ASSERT_NE(game, nullptr);
    ASSERT_NE(sente, nullptr);
    ASSERT_NE(gote, nullptr);
    ASSERT_NE(info, nullptr);
    game->startGame(300, 0);
    game->getClock()->stop();
    QTest::qWait(50);

    auto expectWeights = [&] {
        EXPECT_TRUE(sente->font().bold());
        EXPECT_FALSE(gote->font().bold());
        EXPECT_FALSE(info->font().bold());
    };
    expectWeights();
    window.showMaximized();
    QTest::qWait(50);
    expectWeights();
    window.showNormal();
    QTest::qWait(50);
    expectWeights();
    const int senteSize = sente->fontInfo().pixelSize();
    const int goteSize = gote->fontInfo().pixelSize();
    game->getClock()->setTime(299, 300, Player::Sente);
    QTest::qWait(50);
    expectWeights();
    EXPECT_EQ(sente->fontInfo().pixelSize(), senteSize);
    EXPECT_EQ(gote->fontInfo().pixelSize(), goteSize);

    game->pauseGame();
    QTest::qWait(50);
    EXPECT_FALSE(sente->font().bold());
    EXPECT_FALSE(gote->font().bold());
    game->resumeGame();
    game->getClock()->stop();
    EXPECT_TRUE(sente->font().bold());
    EXPECT_FALSE(gote->font().bold());
    EXPECT_EQ(sente->fontInfo().pixelSize(), senteSize);
    EXPECT_EQ(gote->fontInfo().pixelSize(), goteSize);
    ASSERT_TRUE(game->makeMove(Move::makeMove(2, 4, 2, 3, Player::Sente)));
    QTest::qWait(20);
    EXPECT_FALSE(sente->font().bold());
    EXPECT_TRUE(gote->font().bold());
    EXPECT_EQ(sente->fontInfo().pixelSize(), senteSize);
    EXPECT_EQ(gote->fontInfo().pixelSize(), goteSize);
    window.showMaximized();
    QTest::qWait(20);
    window.showNormal();
    QTest::qWait(20);
    EXPECT_FALSE(sente->font().bold());
    EXPECT_TRUE(gote->font().bold());
}

TEST_F(UILayoutTest, ClockTextChangesDoNotResizeSidebarOrBoard) {
    UIController window(nullptr, false);
    window.resize(800, 600);
    window.show();
    auto* game = window.findChild<GameEngine*>();
    auto* sente = window.findChild<QLabel*>("lblSenteTurn");
    auto* view = window.findChild<QGraphicsView*>();
    ASSERT_NE(game, nullptr);
    ASSERT_NE(sente, nullptr);
    ASSERT_NE(view, nullptr);
    auto* group = qobject_cast<QGroupBox*>(sente->parentWidget());
    ASSERT_NE(group, nullptr);
    game->startGame(300, 0);
    game->getClock()->stop();
    game->getClock()->setTime(60000, 60000, Player::Sente);
    QTest::qWait(50);
    const int clockWidth = group->width();
    const int clockHeight = group->height();
    const int boardWidth = view->width();
    const QSize windowSize = window.size();
    for (int seconds : {5999, 600, 599, 60, 59, 11, 8}) {
        game->getClock()->setTime(seconds, seconds, Player::Sente);
        QTest::qWait(20);
        EXPECT_EQ(group->width(), clockWidth) << "seconds=" << seconds;
        EXPECT_EQ(group->height(), clockHeight) << "seconds=" << seconds;
        EXPECT_EQ(view->width(), boardWidth) << "seconds=" << seconds;
        EXPECT_EQ(window.size(), windowSize);
    }
    // Cross the elapsed-time 99:59 -> 100:00 digit boundary without sleeping.
    for (int tick = 0; tick < 6000; ++tick) {
        ASSERT_TRUE(QMetaObject::invokeMethod(game, "onElapsedTimerTick", Qt::DirectConnection));
    }
    game->getClock()->setTime(8, 8, Player::Sente);
    QTest::qWait(20);
    EXPECT_EQ(group->width(), clockWidth);
    EXPECT_EQ(group->height(), clockHeight);
    EXPECT_EQ(view->width(), boardWidth);
    for (auto* label : group->findChildren<QLabel*>()) {
        EXPECT_FALSE(label->hasHeightForWidth());
        EXPECT_EQ(label->text().count('\n'), 1);
        EXPECT_GE(label->height(), label->sizeHint().height());
    }
}

TEST_F(UILayoutTest, ClockGeometryStaysStableAcrossTurnsAndWindowStates) {
    UIController window(nullptr, false);
    window.show();
    auto* game = window.findChild<GameEngine*>();
    auto* sente = window.findChild<QLabel*>("lblSenteTurn");
    ASSERT_NE(game, nullptr);
    ASSERT_NE(sente, nullptr);
    auto* group = qobject_cast<QGroupBox*>(sente->parentWidget());
    ASSERT_NE(group, nullptr);
    game->startGame(300, 0);
    game->getClock()->stop();
    for (const QSize size : {QSize(800, 600), QSize(1000, 750), QSize(1280, 960)}) {
        window.resize(size);
        QTest::qWait(30);
        const QSize clockSize = group->size();
        auto checkClock = [&] {
            EXPECT_EQ(group->size(), clockSize);
            for (auto* label : group->findChildren<QLabel*>()) {
                EXPECT_GE(label->height(), label->sizeHint().height());
            }
        };
        game->pauseGame();
        QTest::qWait(20);
        checkClock();
        game->resumeGame();
        game->getClock()->stop();
        QTest::qWait(20);
        checkClock();
        ASSERT_TRUE(game->makeMove(Move::makeMove(2, 4, 2, 3, Player::Sente)));
        QTest::qWait(20);
        checkClock();
        game->undo();
        QTest::qWait(20);
        checkClock();
        window.showMaximized();
        QTest::qWait(30);
        window.showNormal();
        QTest::qWait(30);
        checkClock();
    }
}

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
