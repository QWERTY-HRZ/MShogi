#include "../include/UIController.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QMessageBox>

UIController::UIController(QWidget *parent)
    : QMainWindow(parent), m_secondsElapsed(0), m_currentPlayer(Player::Sente)
{
    m_gameEngine = new GameEngine(this);
    m_scene = new GameScene(m_gameEngine, this);

    // 注入回调
    m_scene->setMoveRequestCallback([this](const Move& m) {
        return this->handleMoveRequest(m);
    });

    setupUi();

    connect(m_gameEngine, &GameEngine::stateChanged, this, &UIController::onStateChanged);
    connect(m_gameEngine, &GameEngine::moveExecuted, this, &UIController::onMoveExecuted);
    connect(m_gameEngine, &GameEngine::gameEnded, this, &UIController::onGameEnded);
    // 连接悔棋信号
    connect(m_gameEngine, &GameEngine::undoExecuted, this, &UIController::onUndoExecuted);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &UIController::updateTimer);

    m_gameEngine->startGame();
}

bool UIController::handleMoveRequest(const Move& move) {
    // 校验轮次：只能操作己方棋子
    if (move.player != m_currentPlayer) return false;

    return m_gameEngine->makeMove(move);
}

void UIController::setupUi() {
    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    QHBoxLayout* mainLayout = new QHBoxLayout(centralWidget);

    m_view = new QGraphicsView(m_scene);
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setDragMode(QGraphicsView::NoDrag);
    mainLayout->addWidget(m_view, 2);

    QVBoxLayout* sideLayout = new QVBoxLayout();

    QGroupBox* statusGroup = new QGroupBox("对局信息");
    QVBoxLayout* statusLayout = new QVBoxLayout(statusGroup);
    m_lblStatus = new QLabel("状态: 初始化");
    m_lblTimer = new QLabel("时间: 00:00");
    QFont font = m_lblTimer->font();
    font.setPointSize(14);
    m_lblTimer->setFont(font);
    statusLayout->addWidget(m_lblStatus);
    statusLayout->addWidget(m_lblTimer);
    sideLayout->addWidget(statusGroup);

    QGroupBox* historyGroup = new QGroupBox("棋谱");
    QVBoxLayout* historyLayout = new QVBoxLayout(historyGroup);
    m_txtHistory = new QTextEdit();
    m_txtHistory->setReadOnly(true);
    historyLayout->addWidget(m_txtHistory);
    sideLayout->addWidget(historyGroup);

    QPushButton* btnUndo = new QPushButton("悔棋");
    connect(btnUndo, &QPushButton::clicked, m_gameEngine, &GameEngine::undo);
    sideLayout->addWidget(btnUndo);

    QPushButton* btnRestart = new QPushButton("重新开始");
    connect(btnRestart, &QPushButton::clicked, this, &UIController::onRestartClicked);
    sideLayout->addWidget(btnRestart);

    QPushButton* btnResign = new QPushButton("认输");
    connect(btnResign, &QPushButton::clicked, this, &UIController::onResignClicked);
    sideLayout->addWidget(btnResign);

    mainLayout->addLayout(sideLayout, 1);
    resize(1024, 768);
}

void UIController::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (m_view && m_scene) m_view->fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
}

void UIController::onStateChanged(GameState newState) {
    m_scene->refreshBoard();
    switch (newState) {
        case GameState::Playing:
            // 删除强制重置为先手的代码 防止影响悔棋逻辑
            // m_currentPlayer = Player::Sente;
            // 行棋次序只由 onMoveExecuted/onUndoExecuted 更新
            if (m_lblStatus->text() == "状态: 初始化" || m_lblStatus->text() == "状态: 结束") {
                 m_lblStatus->setText("状态: 对局中");
            }

            if (!m_timer->isActive()) m_timer->start(1000);
            break;
        case GameState::End:
            m_lblStatus->setText("状态: 结束");
            m_timer->stop();
            break;
        default: break;
    }
}

void UIController::onMoveExecuted(const std::string& notation) {
    m_txtHistory->append(QString::fromStdString(notation));
    // 切换回合
    m_currentPlayer = (m_currentPlayer == Player::Sente) ? Player::Gote : Player::Sente;

    QString turnText = (m_currentPlayer == Player::Sente) ? "状态: 先手回合" : "状态: 后手回合";
    QString colorStyle = (m_currentPlayer == Player::Sente) ? "color: red;" : "color: blue;";

    m_lblStatus->setText(turnText);
    m_lblStatus->setStyleSheet(colorStyle + "font-weight: bold;");

    m_scene->refreshBoard();
}

void UIController::onGameEnded(int result) {
    QString msg = (result == 1) ? "先手获胜！" : "后手获胜！";
    QMessageBox::information(this, "对局结束", msg);
}

void UIController::updateTimer() {
    m_secondsElapsed++;
    int min = m_secondsElapsed / 60;
    int sec = m_secondsElapsed % 60;
    m_lblTimer->setText(QString("时间: %1:%2").arg(min, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0')));
}

// 处理悔棋时 UI 变化
void UIController::onUndoExecuted() {
    // 刷新棋盘视图
    m_scene->refreshBoard();
    // 切回上一位玩家
    m_currentPlayer = (m_currentPlayer == Player::Sente) ? Player::Gote : Player::Sente;
    // 更新状态栏文本/颜色
    QString turnText = (m_currentPlayer == Player::Sente) ? "状态: 先手回合" : "状态: 后手回合";
    QString colorStyle = (m_currentPlayer == Player::Sente) ? "color: black;" : "color: red;";
    m_lblStatus->setText(turnText);
    m_lblStatus->setStyleSheet(colorStyle + "font-weight: bold;");
    // 回退棋谱 移动光标到文末/删除选中内容
    QTextCursor cursor = m_txtHistory->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.select(QTextCursor::BlockUnderCursor);
    cursor.removeSelectedText();
    cursor.deleteChar();
}

void UIController::onRestartClicked() {
    // 弹出确认对话框
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "重新开始", "确定要重置当前对局吗？",
                                  QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        // 重置计时器数据
        m_secondsElapsed = 0;
        updateTimer();
        // 清空棋谱
        m_txtHistory->clear();
        // 重置当前执子方为先手
        m_currentPlayer = Player::Sente;
        // 重置棋盘数据
        m_gameEngine->startGame();
        // 刷新状态栏
        m_lblStatus->setText("状态: 先手回合");
        m_lblStatus->setStyleSheet("color: black; font-weight: bold;");
    }
}

void UIController::onResignClicked() {
    // 只有游戏中才能认输
    if (m_gameEngine->getCurrentState() != GameState::Playing) return;
    // 确认按钮
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "认输", "确定要投降吗？", QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        // 判定胜负：当前回合方认输，则对方获胜
        int result = (m_currentPlayer == Player::Sente) ? 2 : 1;
        // 调用引擎结束游戏
        m_gameEngine->finishGame(result);
    }
}
