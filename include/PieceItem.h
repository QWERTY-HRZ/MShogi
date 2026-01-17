#pragma once

#include <QGraphicsObject>
#include <QBrush>
#include <QPen>
#include "Piece.h" // 引用逻辑层的定义

// 前置声明
class GameScene;

class PieceItem : public QGraphicsObject {
    Q_OBJECT
public:
    enum Location { OnBoard, InHand };

    PieceItem(PieceType type, Player owner, Location loc, int gridX, int gridY, int cellSize);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    // 获取逻辑属性
    PieceType getType() const { return m_type; }
    Player getOwner() const { return m_owner; }
    Location getLocation() const { return m_location; }
    int getGridX() const { return m_gridX; }
    int getGridY() const { return m_gridY; }

    // 更新网格位置 (不立即改变坐标，等待刷新)
    void setGridPos(int x, int y);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

private:
    PieceType m_type;
    Player m_owner;
    Location m_location;
    int m_gridX; // -1 if in hand
    int m_gridY; // -1 if in hand
    int m_cellSize;
    
    QPointF m_dragStartPos; // 记录拖拽前的场景坐标，用于回弹
};