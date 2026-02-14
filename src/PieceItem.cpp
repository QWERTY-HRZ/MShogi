#include "../include/PieceItem.h"
#include "../include/GameScene.h"
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QCursor>

PieceItem::PieceItem(PieceType type, Player owner, Location loc, int gridX, int gridY, int cellSize)
    : m_type(type), m_owner(owner), m_location(loc), 
      m_gridX(gridX), m_gridY(gridY), m_cellSize(cellSize) 
{
    setFlag(ItemIsMovable); // 允许拖拽
    setFlag(ItemSendsGeometryChanges);
    setCursor(Qt::OpenHandCursor);
    setZValue(1); // 确保棋子在棋盘上方
}

QRectF PieceItem::boundingRect() const {
    // 稍微缩小一点，留出边距
    return QRectF(5, 5, m_cellSize - 10, m_cellSize - 10);
}

void PieceItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) {
    // 模拟绘制：实际项目中应使用 drawPixmap 加载图片
    // 简单的梯形或矩形表示
    QRectF rect = boundingRect();
    // 区分颜色 先红后黑
    QColor color = (m_owner == Player::Sente) ? Qt::red : Qt::black;
    QPen pen(color);
    pen.setWidth(2);
    painter->setPen(pen);
    painter->setBrush(Qt::white);

    // 绘制棋子形状（简单的倒梯形模拟将棋棋子）
    QPolygonF shape;
    if (m_owner == Player::Sente) { // 先手向上
        shape << QPointF(rect.left() + 10, rect.bottom())
              << QPointF(rect.right() - 10, rect.bottom())
              << QPointF(rect.right(), rect.top() + 15)
              << QPointF(rect.center().x(), rect.top())
              << QPointF(rect.left(), rect.top() + 15);
    } else { // 后手向下
        shape << QPointF(rect.left() + 10, rect.top())
              << QPointF(rect.right() - 10, rect.top())
              << QPointF(rect.right(), rect.bottom() - 15)
              << QPointF(rect.center().x(), rect.bottom())
              << QPointF(rect.left(), rect.bottom() - 15);
    }
    painter->drawPolygon(shape);

    // 绘制文字
    painter->setPen(color);
    QFont font = painter->font();
    font.setPixelSize(m_cellSize / 3);
    painter->setFont(font);
    
    QString text;
    switch (m_type) {
        case PieceType::King: text = "王"; break;
        case PieceType::Rook: text = "车"; break;
        case PieceType::Bishop: text = "相"; break;
        case PieceType::Pawn: text = "兵"; break;
        case PieceType::Hou: text = "侯"; break;
    }
    // 简单的反转文字处理
    painter->drawText(rect, Qt::AlignCenter, text);
}

void PieceItem::setGridPos(int x, int y) {
    m_gridX = x;
    m_gridY = y;
}

void PieceItem::mousePressEvent(QGraphicsSceneMouseEvent *event) {
    // 棋子显示轨迹
    if (auto gameScene = dynamic_cast<GameScene*>(scene())) {
        gameScene->toggleHighlight(this);
    }
    m_dragStartPos = pos(); // 记录起始位置
    setCursor(Qt::ClosedHandCursor);
    setZValue(10); // 拖拽时置顶
    QGraphicsObject::mousePressEvent(event);
}

void PieceItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
    setCursor(Qt::OpenHandCursor);
    setZValue(1);
    QGraphicsObject::mouseReleaseEvent(event);

    // 通知 Scene 处理落子尝试
    GameScene* scenePtr = dynamic_cast<GameScene*>(scene());
    if (scenePtr) {
        // 如果处理失败（非法移动），回弹
        if (!scenePtr->handlePieceDrop(this, event->scenePos())) {
            setPos(m_dragStartPos);
        }
    } else {
        setPos(m_dragStartPos);
    }
}
