#pragma once

namespace GameConstants {
    // 棋盘行/列
    static constexpr int COLS = 5;
    static constexpr int ROWS = 6;
    
    // 先/后手底线
    static constexpr int SENTE_BASE_Y = ROWS - 1;
    static constexpr int GOTE_BASE_Y = 0;
    
    // 打入区域
    static constexpr int ZONE_HEIGHT = 3;
    
    // UI 单元格大小
    static constexpr int CELL_SIZE = 80;
    // UI 棋盘偏移
    static constexpr int BOARD_OFFSET_X = 50;
    static constexpr int BOARD_OFFSET_Y = 150;

    // 拖拽的X/Y轴最小位移 小于此值视为点击
    static constexpr double MIN_DRAG_DISTANCE = 15.0;
}
