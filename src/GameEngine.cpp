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
    if (m_currentState != GameState::Playing) return false;

    // 1. 获取上一手被吃掉的棋子作为打入禁手（如果存在）
    std::optional<PieceType> forbidden;
    if (auto lastNode = m_history.peek()) {
        if (lastNode->capturedType.has_value()) forbidden = lastNode->capturedType.value();
    }

    // 2. 验证移动合法性
    if (!m_ruleEngine.validateMove(m_board, move, forbidden)) return false;

    // 3. 准备状态变量
    std::optional<PieceType> capturedOriginalType = std::nullopt;
    bool isPromoted = false;
    bool shouldPromote = false;

    // 4. 执行逻辑
    if (move.isDrop) {
        if (!m_board.removeFromHand(move.player, move.dropType)) return false;
        m_board.placePiece(move.toX, move.toY, createPiece(move.dropType, move.player));
    } else {
        // 在移动前判断升变条件，因为需要读取 fromY 的位置
        shouldPromote = m_ruleEngine.checkPromotion(m_board, move);

        // 处理吃子：记录原始类型，并降变存入手驹
        auto target = m_board.getPiece(move.toX, move.toY);
        if (target) {
            capturedOriginalType = target->getType();
            PieceType handType = (capturedOriginalType.value() == PieceType::Hou)
                                 ? PieceType::Pawn : capturedOriginalType.value();
            m_board.addToHand(move.player, handType);
        }

        // 执行物理移动
        m_board.movePiece(move.fromX, move.fromY, move.toX, move.toY);

        // 如果满足条件，执行升变（替换为 Hou）
        if (shouldPromote) {
            m_board.removePiece(move.toX, move.toY);
            m_board.placePiece(move.toX, move.toY, createPiece(PieceType::Hou, move.player));
            isPromoted = true;
        }
    }

    // 5. 生成记谱（基于移动完成后的最终盘面）
    std::string notation = MoveHistory::generateNotation(m_board, move, m_ruleEngine);

    // 6. 压入历史栈
    m_history.push({move, capturedOriginalType, isPromoted, notation});
    emit moveExecuted(notation);

    // 7. 判定胜负
    int res = m_ruleEngine.isGameOver(m_board);
    if (res != 0) finishGame(res);

    return true;
}

void GameEngine::undo() {
    if (!m_history.canUndo()) return;

    auto nodeOpt = m_history.pop();
    if (!nodeOpt) return;
    const HistoryNode& node = nodeOpt.value();

    if (node.move.isDrop) {
        // 撤销打入：移除棋子，放回手驹
        m_board.removePiece(node.move.toX, node.move.toY);
        m_board.addToHand(node.move.player, node.move.dropType);
    } else {
        // 获取移动后的棋子（可能是升变后的）
        auto movedPiece = m_board.getPiece(node.move.toX, node.move.toY);
        m_board.removePiece(node.move.toX, node.move.toY);

        // 推断复原类型：若发生了升变，原类型必为 Pawn，否则保持当前类型
        PieceType restoreType = node.isPromoted ? PieceType::Pawn : movedPiece->getType();

        // 棋子归位
        m_board.placePiece(node.move.fromX, node.move.fromY, createPiece(restoreType, node.move.player));

        // 恢复被吃掉的棋子
        if (node.capturedType.has_value()) {
            PieceType originalCapType = node.capturedType.value();

            // 从当前玩家手驹扣除（注意：Hou 在手驹中是 Pawn）
            PieceType handType = (originalCapType == PieceType::Hou) ? PieceType::Pawn : originalCapType;
            m_board.removeFromHand(node.move.player, handType);

            // 归还给敌方（归还原始类型）
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
    std::vector<Move> moves;
    auto piece = m_board.getPiece(x, y);
    if (!piece) return moves;

    // 遍历棋盘所有格子尝试移动
    for (int tx = 0; tx < Board::COLS; ++tx) {
        for (int ty = 0; ty < Board::ROWS; ++ty) {
            Move m = Move::makeMove(x, y, tx, ty, piece->getOwner());
            // 因为只是预测，不考虑上一手的禁手
            if (m_ruleEngine.validateMove(m_board, m, std::nullopt)) {
                moves.push_back(m);
            }
        }
    }
    return moves;
}

std::vector<Move> GameEngine::getLegalDrops(PieceType type) {
    std::vector<Move> moves;
    Player player = getCurrentPlayer();
    // 获取上一手被吃掉的棋子 判断打入禁手
    std::optional<PieceType> forbidden;
    if (auto lastNode = m_history.peek()) {
        if (lastNode->capturedType.has_value()) forbidden = lastNode->capturedType.value();
    }

    for (int x = 0; x < GameConstants::COLS; ++x) {
        for (int y = 0; y < GameConstants::ROWS; ++y) {
            // 剪枝：格子非空时 直接跳过
            if (m_board.getPiece(x, y) != nullptr) continue;

            Move m = Move::makeDrop(x, y, type, player);
            if (m_ruleEngine.validateMove(m_board, m, forbidden)) {
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
    // 默认为先手
    if (!m_history.peek().has_value()) return Player::Sente;
    // 否则返回【上一手玩家的对手】
    return (m_history.peek()->move.player == Player::Sente) ? Player::Gote : Player::Sente;
}
