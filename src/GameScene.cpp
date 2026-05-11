#include "../include/GameScene.h"
#include <QGraphicsLineItem>
#include <QBrush>
#include <QPixmap>

GameScene::GameScene(GameEngine* engine, QObject* parent)
    : QGraphicsScene(parent), m_engine(engine)
{
    // 500, 800
    int w = BOARD_OFFSET_X * 2 + GameConstants::COLS * CELL_SIZE;
    int h = BOARD_OFFSET_Y * 2 + GameConstants::ROWS * CELL_SIZE;
    setSceneRect(0, 0, w, h);
}

void GameScene::refreshBoard() {
    // 重置选中棋子、状态
    m_selectedPiece = nullptr;
    m_highlights.clear();
    clear();
    drawGrid();
    drawPieces();
    drawHands();
}

void GameScene::drawGrid() {
    QPen pen(Qt::white, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    for (int i = 0; i <= GameConstants::COLS; ++i)
        addLine(BOARD_OFFSET_X + i * CELL_SIZE, BOARD_OFFSET_Y,
                 BOARD_OFFSET_X + i * CELL_SIZE, BOARD_OFFSET_Y + GameConstants::ROWS * CELL_SIZE, pen);
    for (int j = 0; j <= GameConstants::ROWS; ++j)
        addLine(BOARD_OFFSET_X, BOARD_OFFSET_Y + j * CELL_SIZE,
                 BOARD_OFFSET_X + GameConstants::COLS * CELL_SIZE, BOARD_OFFSET_Y + j * CELL_SIZE, pen);
}

void GameScene::drawPieces() {
    const auto& board = m_engine->getBoard();
    for (int x = 0; x < Board::COLS; ++x) {
        for (int y = 0; y < Board::ROWS; ++y) {
            auto p = board.getPiece(x, y);
            if (p) {
                // 修正：直接使用 y，不反转
                auto item = new PieceItem(p->getType(), p->getOwner(), PieceItem::OnBoard, x, y, CELL_SIZE);
                item->setPos(gridToScene(x, y));
                addItem(item);
            }
        }
    }
}

void GameScene::drawHands() {
    // 绘制后手手驹区 在上方
    int gX = BOARD_OFFSET_X, gY = 20;
    // 获取棋子列表 直接遍历 后同
    const auto& goteHand = m_engine->getBoard().getHand(Player::Gote);
    for (const auto& piece : goteHand) {
        auto item = new PieceItem(piece->getType(), Player::Gote, PieceItem::InHand, -1, -1, GameConstants::CELL_SIZE);
        item->setPos(gX, gY);
        // 检查回合数 为禁手期棋子添加灰色蒙版
        int t = piece->getTurnsInHand();
        if (t >= 1 && t <= 3) item->setForbidden(true);
        addItem(item);
        gX += GameConstants::CELL_SIZE + 10;
    }
    // 绘制先手手驹区
    int sX = BOARD_OFFSET_X, sY = BOARD_OFFSET_Y + GameConstants::ROWS * CELL_SIZE + 20;
    const auto& senteHand = m_engine->getBoard().getHand(Player::Sente);
    // 其余逻辑相同
    for (const auto& piece : senteHand) {
        auto item = new PieceItem(piece->getType(), Player::Sente, PieceItem::InHand, -1, -1, GameConstants::CELL_SIZE);
        item->setPos(sX, sY);
        int t = piece->getTurnsInHand();
        if (t >= 1 && t <= 3) item->setForbidden(true);
        addItem(item);
        sX += GameConstants::CELL_SIZE + 10;
    }
}

QPointF GameScene::gridToScene(int x, int y) const {
    // 修正：直接映射 Y 坐标，0为顶，5为底
    return QPointF(BOARD_OFFSET_X + x * CELL_SIZE, BOARD_OFFSET_Y + y * CELL_SIZE);
}

bool GameScene::sceneToGrid(QPointF pos, int &x, int &y) const {
    int rx = (pos.x() - BOARD_OFFSET_X) / CELL_SIZE;
    int ry = (pos.y() - BOARD_OFFSET_Y) / CELL_SIZE;
    // 修正：直接使用 ry，不反转
    if (rx >= 0 && rx < GameConstants::COLS && ry >= 0 && ry < GameConstants::ROWS) {
        x = rx;
        y = ry;
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

    if (m_requestCallback) {
        return m_requestCallback(move);
    }
    return false;
}

void GameScene::toggleHighlight(PieceItem* item) {
    // 清除旧的高亮
    for (auto rect : m_highlights) delete rect;
    m_highlights.clear();

    // 点击已选中的棋子，取消选中
    if (m_selectedPiece == item) {
        m_selectedPiece = nullptr;
        return;
    }

    // 只有当前回合玩家的棋子才能高亮
    if (item->getOwner() != m_engine->getCurrentPlayer()) return;
    // 获取合法移动、打入位置
    m_selectedPiece = item;
    std::vector<Move> moves;
    if (item->getLocation() == PieceItem::OnBoard) {
        moves = m_engine->getLegalMoves(item->getGridX(), item->getGridY());
    } else {
        moves = m_engine->getLegalDrops(item->getType());
    }
    // 绘制高亮
    for (const auto& move : moves) {
        bool isCapture = false;
        // 判断是否有吃子
        if (!move.isDrop) {
            isCapture = (m_engine->getBoard().getPiece(move.toX, move.toY) != nullptr);
        }
        // 移动、吃子标记为：浅红、浅绿
        QColor color = isCapture ? QColor(255, 180, 180, 150) : QColor(180, 255, 180, 150);
        // 计算场景坐标
        QPointF pos(BOARD_OFFSET_X + move.toX * CELL_SIZE, BOARD_OFFSET_Y + move.toY * CELL_SIZE);
        auto rect = addRect(pos.x(), pos.y(), CELL_SIZE, CELL_SIZE, Qt::NoPen, QBrush(color));
        // 高亮位置：网格(0)之上，棋子(1)之下
        rect->setZValue(0.5);
        m_highlights.append(rect);
    }
}

bool GameScene::isGamePlaying() const {
    return m_engine->getCurrentState() == GameState::Playing;
}
