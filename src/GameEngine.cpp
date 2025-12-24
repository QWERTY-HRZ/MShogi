#include "../include/GameEngine.h"

GameEngine::GameEngine(QObject *parent)
    : QObject(parent), m_currentState(GameState::Init) {}

void GameEngine::startGame() {
    m_board.clear();

    // Sente (Base Y=0)
    m_board.placePiece(2, 0, createPiece(PieceType::King, Player::Sente));
    m_board.placePiece(0, 0, createPiece(PieceType::Bishop, Player::Sente));
    m_board.placePiece(4, 0, createPiece(PieceType::Rook, Player::Sente));
    m_board.placePiece(2, 1, createPiece(PieceType::Pawn, Player::Sente));
    m_board.placePiece(0, 1, createPiece(PieceType::Pawn, Player::Sente));
    m_board.placePiece(4, 1, createPiece(PieceType::Pawn, Player::Sente));

    // Gote (Base Y=5)
    m_board.placePiece(2, 5, createPiece(PieceType::King, Player::Gote));
    m_board.placePiece(4, 5, createPiece(PieceType::Bishop, Player::Gote));
    m_board.placePiece(0, 5, createPiece(PieceType::Rook, Player::Gote));
    m_board.placePiece(2, 4, createPiece(PieceType::Pawn, Player::Gote));
    m_board.placePiece(4, 4, createPiece(PieceType::Pawn, Player::Gote));
    m_board.placePiece(0, 4, createPiece(PieceType::Pawn, Player::Gote));

    m_currentState = GameState::Playing;
    emit stateChanged(m_currentState);
}

bool GameEngine::makeMove(const Move& move) {
    if (m_currentState != GameState::Playing && m_currentState != GameState::PromotionCheck) return false;

    // 获取禁手限制并验证
    std::optional<PieceType> forbidden;
    if (auto lastNode = m_history.peek()) {
        if (lastNode->capturedType.has_value()) {
            forbidden = lastNode->capturedType.value();
        }
    }
    if (!m_ruleEngine.validateMove(m_board, move, forbidden)) {
        return false;
    }

    // 生成记谱
    std::string notation = MoveHistory::generateNotation(m_board, move, m_ruleEngine);

    // 执行移动流程
    std::optional<PieceType> capturedType = std::nullopt;
    bool isPromoted = false;
    bool shouldPromote = false; // 新增：提前记录是否应该升变

    // 移动流程
    if (move.isDrop) {
        if (!m_board.removeFromHand(move.player, move.dropType)) return false;
        m_board.placePiece(move.toX, move.toY, createPiece(move.dropType, move.player));
    } else {
        // 在 movePiece 之前检查升变！
        // 因为 checkPromotion 需要读取 move.fromX/fromY 处的棋子类型
        // 如果移动后再查，from 处已被清空，会导致 checkPromotion 恒为 false
        shouldPromote = m_ruleEngine.checkPromotion(m_board, move);

        // 检查吃子
        auto target = m_board.getPiece(move.toX, move.toY);
        if (target) {
            PieceType capturedRaw = target->getType();
            if (capturedRaw == PieceType::Hou) capturedRaw = PieceType::Pawn;
            capturedType = capturedRaw;
            m_board.addToHand(move.player, capturedRaw);
        }

        // 执行物理移动 (覆盖目标格，源置空)
        m_board.movePiece(move.fromX, move.fromY, move.toX, move.toY);

        // 如果满足升变条件，执行升变
        // 这一步在移动和吃子之后，但在胜负判定之前
        if (shouldPromote) {
             m_board.removePiece(move.toX, move.toY);
             m_board.placePiece(move.toX, move.toY, createPiece(PieceType::Hou, move.player));
             isPromoted = true;
        }
    }

    // 记录棋谱
    HistoryNode node{move, capturedType, isPromoted, notation};
    m_history.push(node);
    emit moveExecuted(notation);

    // 判定胜负
    // 此时棋盘状态已更新：若这步棋吃掉了王，isGameOver 会检测到王不存在，判定胜利
    // 若这步棋是下底，isGameOver 会根据 Flag 更新逻辑处理
    int res = m_ruleEngine.isGameOver(m_board);
    if (res != 0) {
        finishGame(res);
    }

    return true;
}

void GameEngine::undo() {
    if (!m_history.canUndo()) return;

    auto nodeOpt = m_history.pop();
    if (!nodeOpt) return;

    const HistoryNode& node = nodeOpt.value();

    if (node.move.isDrop) {
        m_board.removePiece(node.move.toX, node.move.toY);
        m_board.addToHand(node.move.player, node.move.dropType);
    } else {
        auto piece = m_board.removePiece(node.move.toX, node.move.toY);

        // 如果这步棋发生了升变，撤销时要降变回兵
        if (node.isPromoted) {
            piece = createPiece(PieceType::Pawn, node.move.player);
        }
        m_board.placePiece(node.move.fromX, node.move.fromY, piece);

        // 恢复被吃子
        if (node.capturedType.has_value()) {
            PieceType capType = node.capturedType.value();
            m_board.removeFromHand(node.move.player, capType);
            Player enemy = (node.move.player == Player::Sente) ? Player::Gote : Player::Sente;
             m_board.placePiece(node.move.toX, node.move.toY, createPiece(capType, enemy));
        }
    }

    // 如果是从结束状态悔棋，恢复到进行中
    if (m_currentState == GameState::End) {
        m_currentState = GameState::Playing;
        emit stateChanged(m_currentState);
    }
}

void GameEngine::redo() {}

void GameEngine::finishGame(int result) {
    m_currentState = GameState::End;
    emit stateChanged(m_currentState);
    emit gameEnded(result);
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
