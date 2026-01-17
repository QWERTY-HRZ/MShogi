#pragma once
#include <QGraphicsScene>
#include <functional> // 引入 std::function
#include "GameEngine.h"
#include "PieceItem.h"

class GameScene : public QGraphicsScene {
    Q_OBJECT
public:
    // 定义回调函数类型：输入Move，返回bool(成功/失败)
    using MoveRequestCallback = std::function<bool(const Move&)>;

    GameScene(GameEngine* engine, QObject* parent = nullptr);

    void refreshBoard();
    bool handlePieceDrop(PieceItem* item, QPointF dropPos);

    // 设置回调函数
    void setMoveRequestCallback(MoveRequestCallback cb) { m_requestCallback = cb; }

    static constexpr int CELL_SIZE = 80;
    static constexpr int BOARD_OFFSET_X = 50;
    static constexpr int BOARD_OFFSET_Y = 150;

private:
    GameEngine* m_engine; // 仅用于读取数据(绘制)
    MoveRequestCallback m_requestCallback; // 用于发送移动请求

    QPointF gridToScene(int x, int y) const;
    bool sceneToGrid(QPointF pos, int &x, int &y) const;

    void drawGrid();
    void drawPieces();
    void drawHands();
};
