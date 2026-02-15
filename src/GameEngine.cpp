#include "../include/GameEngine.h"

GameEngine::GameEngine(QObject *parent)
    : QObject(parent), m_currentState(GameState::Init) {}

void GameEngine::startGame() {
    m_board.clear();
    const int maxY = GameConstants::SENTE_BASE_Y;
    const int minY = GameConstants::GOTE_BASE_Y;

    // Sente (先手，位于下方 Y=4,5，从下往上攻)
    m_board.placePiece(2, maxY, createPiece(PieceType::King, Player::Sente));
    m_board.placePiece(0, maxY, createPiece(PieceType::Bishop, Player::Sente));
    m_board.placePiece(4, maxY, createPiece(PieceType::Rook, Player::Sente));
    m_board.placePiece(0, maxY - 1, createPiece(PieceType::Pawn, Player::Sente));
    m_board.placePiece(2, maxY - 1, createPiece(PieceType::Pawn, Player::Sente));
    m_board.placePiece(4, maxY - 1, createPiece(PieceType::Pawn, Player::Sente));

    // Gote (后手，位于上方 Y=0,1，从上往下攻)
    // 对应Sente
    m_board.placePiece(2, minY, createPiece(PieceType::King, Player::Gote));
    m_board.placePiece(0, minY, createPiece(PieceType::Rook, Player::Gote));
    m_board.placePiece(4, minY, createPiece(PieceType::Bishop, Player::Gote));
    m_board.placePiece(0, minY + 1, createPiece(PieceType::Pawn, Player::Gote));
    m_board.placePiece(2, minY + 1, createPiece(PieceType::Pawn, Player::Gote));
    m_board.placePiece(4, minY + 1, createPiece(PieceType::Pawn, Player::Gote));

    m_currentState = GameState::Playing;
    emit stateChanged(m_currentState);
}

bool GameEngine::makeMove(const Move& move) {
    // 游戏状态
    if (m_currentState != GameState::Playing) return false;
    // 验证移动合法性
    if (!m_ruleEngine.validateMove(m_board, move)) return false;
    // 预备状态变量
    std::optional<PieceType> capturedOriginalType = std::nullopt;
    bool isPromoted = false;
    bool shouldPromote = false;

    // 执行逻辑
    if (move.isDrop) {
        // 打入 已避开禁手棋子
        if (!m_board.removeFromHand(move.player, move.dropType)) return false;
        // 此处无需更改回合数 每次加入手驹都会刷新
        m_board.placePiece(move.toX, move.toY, createPiece(move.dropType, move.player));
    } else {
        // 正常移动 先判断升变
        shouldPromote = m_ruleEngine.checkPromotion(m_board, move);
        // 处理吃子
        auto target = m_board.getPiece(move.toX, move.toY);
        if (target) {
            capturedOriginalType = target->getType();
            // 侯降变回兵
            PieceType handType = (capturedOriginalType.value() == PieceType::Hou)
                                 ? PieceType::Pawn : capturedOriginalType.value();
            // 创建棋子实体 用于存储
            auto newPiece = createPiece(handType, move.player);
            newPiece->setTurnsInHand(1);
            m_board.addToHand(newPiece);
        }
        // 执行物理移动
        m_board.movePiece(move.fromX, move.fromY, move.toX, move.toY);
        // 满足条件时升变
        if (shouldPromote) {
            m_board.removePiece(move.toX, move.toY);
            m_board.placePiece(move.toX, move.toY, createPiece(PieceType::Hou, move.player));
            isPromoted = true;
        }
    }

    // 手驹回合数 +1
    m_board.updateHandTurns(1);
    // 基于移动后的盘面 生成记谱
    std::string notation = MoveHistory::generateNotation(m_board, move, m_ruleEngine);
    // 压入历史栈 发信号
    m_history.push({move, capturedOriginalType, isPromoted, notation});
    emit moveExecuted(notation);
    // 判定胜负
    int res = m_ruleEngine.isGameOver(m_board);
    if (res != 0) finishGame(res);

    return true;
}

void GameEngine::undo() {
    if (!m_history.canUndo()) return;

    auto nodeOpt = m_history.pop();
    if (!nodeOpt) return;
    const HistoryNode& node = nodeOpt.value();
    // 回退时 手驹回合数 -1
    m_board.updateHandTurns(-1);

    if (node.move.isDrop) {
        // 撤销打入：重新创建棋子 放回手驹
        m_board.removePiece(node.move.toX, node.move.toY);
        auto p = createPiece(node.move.dropType, node.move.player);
        // 重置回合数
        p->setTurnsInHand(0);
        m_board.addToHand(p);
    } else {
        // 重置移动后的棋子
        auto piece = m_board.removePiece(node.move.toX, node.move.toY);
        if (node.isPromoted) {
            piece = createPiece(PieceType::Pawn, node.move.player);
        }
        m_board.placePiece(node.move.fromX, node.move.fromY, piece);
        // 如果有吃子 从手驹区恢复
        if (node.capturedType.has_value()) {
            PieceType originalCapType = node.capturedType.value();
            // 侯在手驹中是兵
            PieceType handType = (originalCapType == PieceType::Hou) ? PieceType::Pawn : originalCapType;
            m_board.removeFromHand(node.move.player, handType);
            // 将吃子归还给敌方
            Player enemy = (node.move.player == Player::Sente) ? Player::Gote : Player::Sente;
            m_board.placePiece(node.move.toX, node.move.toY, createPiece(originalCapType, enemy));
        }
    }

    // 如果从结束状态回退，改变状态
    if (m_currentState == GameState::End) {
        m_currentState = GameState::Playing;
        emit stateChanged(m_currentState);
    }
    // 悔棋信号，通知 UI 刷新
    emit undoExecuted();
}

void GameEngine::finishGame(int result) {
    m_currentState = GameState::End;
    emit stateChanged(m_currentState);
    emit gameEnded(result);
}

std::vector<Move> GameEngine::getLegalMoves(int x, int y) {
    // 合法移动
    std::vector<Move> moves;
    auto piece = m_board.getPiece(x, y);
    if (!piece) return moves;

    // 遍历棋盘 尝试移动
    for (int tx = 0; tx < GameConstants::COLS; ++tx) {
        for (int ty = 0; ty < GameConstants::ROWS; ++ty) {
            Move m = Move::makeMove(x, y, tx, ty, piece->getOwner());
            // 不传禁手参数
            if (m_ruleEngine.validateMove(m_board, m)) {
                moves.push_back(m);
            }
        }
    }
    return moves;
}

std::vector<Move> GameEngine::getLegalDrops(PieceType type) {
    std::vector<Move> moves;
    Player player = getCurrentPlayer();

    for (int x = 0; x < GameConstants::COLS; ++x) {
        for (int y = 0; y < GameConstants::ROWS; ++y) {
            // 格子非空时 一定不能打入
            if (m_board.getPiece(x, y) != nullptr) continue;
            Move m = Move::makeDrop(x, y, type, player);
            // 撤销禁手调用
            if (m_ruleEngine.validateMove(m_board, m)) {
                moves.push_back(m);
            }
        }
    }
    return moves;
}

GameState GameEngine::getCurrentState() const { return m_currentState; }
const Board& GameEngine::getBoard() const { return m_board; }
const MoveHistory& GameEngine::getHistory() const { return m_history; }

std::shared_ptr<Piece> GameEngine::createPiece(PieceType type, Player owner) {
    switch (type) {
        case PieceType::King: return std::make_shared<King>(owner);
        case PieceType::Rook: return std::make_shared<Rook>(owner);
        case PieceType::Bishop: return std::make_shared<Bishop>(owner);
        case PieceType::Pawn: return std::make_shared<Pawn>(owner);
        case PieceType::Hou: return std::make_shared<Hou>(owner);
        default: return nullptr;
    }
}

Player GameEngine::getCurrentPlayer() const {
    // 默认为先手 否则返回【上一手玩家的对手】
    if (!m_history.peek().has_value()) return Player::Sente;
    return (m_history.peek()->move.player == Player::Sente) ? Player::Gote : Player::Sente;
}
