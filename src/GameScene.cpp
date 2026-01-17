#include "../include/GameScene.h"
#include <QGraphicsLineItem>

GameScene::GameScene(GameEngine* engine, QObject* parent)
    : QGraphicsScene(parent), m_engine(engine)
{
    setSceneRect(0, 0, 500, 800);
}

void GameScene::refreshBoard() {
    clear();
    drawGrid();
    drawPieces();
    drawHands();
}

void GameScene::drawGrid() {
    QPen pen(Qt::black);
    for (int i = 0; i <= 5; ++i)
        addLine(BOARD_OFFSET_X + i * CELL_SIZE, BOARD_OFFSET_Y,
                 BOARD_OFFSET_X + i * CELL_SIZE, BOARD_OFFSET_Y + 6 * CELL_SIZE, pen);
    for (int j = 0; j <= 6; ++j)
        addLine(BOARD_OFFSET_X, BOARD_OFFSET_Y + j * CELL_SIZE,
                 BOARD_OFFSET_X + 5 * CELL_SIZE, BOARD_OFFSET_Y + j * CELL_SIZE, pen);
}

void GameScene::drawPieces() {
    const auto& board = m_engine->getBoard();
    for (int x = 0; x < Board::COLS; ++x) {
        for (int y = 0; y < Board::ROWS; ++y) {
            auto p = board.getPiece(x, y);
            if (p) {
                auto item = new PieceItem(p->getType(), p->getOwner(), PieceItem::OnBoard, x, y, CELL_SIZE);
                item->setPos(gridToScene(x, y));
                addItem(item);
            }
        }
    }
}

void GameScene::drawHands() {
    const auto& board = m_engine->getBoard();
    int gX = BOARD_OFFSET_X, gY = 20;
    const std::vector<PieceType> types = {PieceType::Rook, PieceType::Bishop, PieceType::Pawn, PieceType::Hou};

    for (auto t : types) {
        int count = board.getHandCount(Player::Gote, t);
        for (int i = 0; i < count; ++i) {
            auto item = new PieceItem(t, Player::Gote, PieceItem::InHand, -1, -1, CELL_SIZE);
            item->setPos(gX, gY);
            addItem(item);
            gX += CELL_SIZE + 10;
        }
    }

    int sX = BOARD_OFFSET_X, sY = BOARD_OFFSET_Y + 6 * CELL_SIZE + 20;
    for (auto t : types) {
        int count = board.getHandCount(Player::Sente, t);
        for (int i = 0; i < count; ++i) {
            auto item = new PieceItem(t, Player::Sente, PieceItem::InHand, -1, -1, CELL_SIZE);
            item->setPos(sX, sY);
            addItem(item);
            sX += CELL_SIZE + 10;
        }
    }
}

QPointF GameScene::gridToScene(int x, int y) const {
    return QPointF(BOARD_OFFSET_X + x * CELL_SIZE, BOARD_OFFSET_Y + (5 - y) * CELL_SIZE);
}

bool GameScene::sceneToGrid(QPointF pos, int &x, int &y) const {
    int rx = (pos.x() - BOARD_OFFSET_X) / CELL_SIZE;
    int ry = (pos.y() - BOARD_OFFSET_Y) / CELL_SIZE;
    if (rx >= 0 && rx < 5 && ry >= 0 && ry < 6) {
        x = rx; y = 5 - ry;
        return true;
    }
    return false;
}

bool GameScene::handlePieceDrop(PieceItem* item, QPointF dropPos) {
    int toX, toY;
    if (!sceneToGrid(dropPos, toX, toY)) return false; // 坐标无效

    Move move;
    if (item->getLocation() == PieceItem::OnBoard) {
        move = Move::makeMove(item->getGridX(), item->getGridY(), toX, toY, item->getOwner());
    } else {
        move = Move::makeDrop(toX, toY, item->getType(), item->getOwner());
    }

    // 通过回调请求控制器处理，解耦 Scene 和 Engine 的写操作
    if (m_requestCallback) {
        return m_requestCallback(move);
    }
    return false;
}
