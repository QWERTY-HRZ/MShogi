#include "../include/UIController.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QInputDialog>

UIController::UIController(QWidget *parent)
    : QMainWindow(parent), m_secondsElapsed(0)
{
    m_gameEngine = new GameEngine(this);
    m_scene = new GameScene(m_gameEngine, this);

    // 注入回调
    m_scene->setMoveRequestCallback([this](const Move& m) {
        return this->handleMoveRequest(m);
    });

    setupUi();
    // 连接信号
    connect(m_gameEngine, &GameEngine::stateChanged, this, &UIController::onStateChanged);
    connect(m_gameEngine, &GameEngine::moveExecuted, this, &UIController::onMoveExecuted);
    connect(m_gameEngine, &GameEngine::gameEnded, this, &UIController::onGameEnded);
    connect(m_gameEngine, &GameEngine::undoExecuted, this, &UIController::onUndoExecuted);
    connect(m_gameEngine->getClock(), &ChessClock::timeUpdated, this, &UIController::onUpdateTimer);

    m_uiTimer = new QTimer(this);
    connect(m_uiTimer, &QTimer::timeout, this, &UIController::onUpdateTimer);
    // 默认开局：5+5
    m_gameEngine->startGame(300, 5);
}

bool UIController::handleMoveRequest(const Move& move) {
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
    // 重构状态栏
    m_lblStatus = new QLabel("状态: 初始化");
    m_lblGameInfo = new QLabel("总用时: 00:00 | 棋钟: -- 分 + --");
    m_lblSenteTurn = new QLabel("先手回合：--");
    m_lblGoteTurn = new QLabel("后手回合：--");
    // 字体大小
    QFont font = m_lblStatus->font();
    font.setPointSize(12);
    m_lblStatus->setFont(font);
    m_lblGameInfo->setFont(font);
    m_lblSenteTurn->setFont(font);
    m_lblGoteTurn->setFont(font);
    // 调整边框
    statusLayout->addWidget(m_lblStatus);
    statusLayout->addWidget(m_lblGameInfo);
    statusLayout->addWidget(m_lblSenteTurn);
    statusLayout->addWidget(m_lblGoteTurn);
    sideLayout->addWidget(statusGroup);
    // 行棋记录
    QGroupBox* historyGroup = new QGroupBox("棋谱");
    QVBoxLayout* historyLayout = new QVBoxLayout(historyGroup);
    m_txtHistory = new QTextEdit();
    m_txtHistory->setReadOnly(true);
    historyLayout->addWidget(m_txtHistory);
    sideLayout->addWidget(historyGroup);
    // 按钮
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
    resize(1280, 960);
}

void UIController::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (m_view && m_scene) m_view->fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
}

void UIController::onStateChanged(GameState newState) {
    m_scene->refreshBoard();
    switch (newState) {
        case GameState::Playing:
            // 不在此处修改行棋顺序
            // m_currentPlayer = Player::Sente;
            m_lblStatus->setText("状态: 对局中");
            if (!m_uiTimer->isActive()) m_uiTimer->start(1000);
            break;
        case GameState::End:
            m_lblStatus->setText("状态: 结束");
            m_uiTimer->stop();
            break;
        default: break;
    }
    onUpdateTimer();
}

// 精简 状态修改放到 onUpdateTimer
void UIController::onMoveExecuted(const std::string& notation) {
    m_txtHistory->append(QString::fromStdString(notation));
    m_scene->refreshBoard();
}

// 精简 状态修改放到 onUpdateTimer
void UIController::onUndoExecuted() {
    m_scene->refreshBoard();
    QTextCursor cursor = m_txtHistory->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.select(QTextCursor::BlockUnderCursor);
    cursor.removeSelectedText();
    cursor.deleteChar();
}

void UIController::onGameEnded(int result) {
    QString msg = (result == 1) ? "先手获胜！" : "后手获胜！";
    QMessageBox::information(this, "对局结束", msg);
}

void UIController::onUpdateTimer() {
    // 仅通过系统定时器累计总用时，避免重复触发棋钟信号
    if (m_gameEngine->getCurrentState() == GameState::Playing && sender() == m_uiTimer) {
        m_secondsElapsed++;
    }
    // 总用时规则
    int totalM = m_secondsElapsed / 60, totalS = m_secondsElapsed % 60;
    int settingM = m_gameEngine->getClock()->getTotalSetting() / 60;
    int inc = m_gameEngine->getClock()->getIncrement();
    // 设置时长/棋钟
    m_lblGameInfo->setText(QString("总用时: %1:%2 | 棋钟: %3分+%4秒")
                           .arg(totalM, 2, 10, QChar('0')).arg(totalS, 2, 10, QChar('0'))
                           .arg(settingM).arg(inc));

    // 获取棋钟数据
    int sTime = m_gameEngine->getClock()->getSenteTime();
    int gTime = m_gameEngine->getClock()->getGoteTime();
    Player curP = m_gameEngine->getCurrentPlayer();
    bool isPlaying = (m_gameEngine->getCurrentState() == GameState::Playing);

    // 第三/四行：剩余时间
    m_lblSenteTurn->setText(QString("先手回合：剩余 %1:%2")
                            .arg(sTime / 60, 2, 10, QChar('0')).arg(sTime % 60, 2, 10, QChar('0')));
    m_lblGoteTurn->setText(QString("后手回合：剩余 %1:%2")
                           .arg(gTime / 60, 2, 10, QChar('0')).arg(gTime % 60, 2, 10, QChar('0')));

    // 根据回合 加粗/改变背景色
    QString baseSente = "color: red;";
    QString baseGote = "color: black;";

    if (isPlaying && curP == Player::Sente) {
        m_lblSenteTurn->setStyleSheet(baseSente + " font-weight: bold; background: #ffe0e0;");
        m_lblGoteTurn->setStyleSheet(baseGote + " font-weight: normal; background: none;");
    } else if (isPlaying && curP == Player::Gote) {
        m_lblSenteTurn->setStyleSheet(baseSente + " font-weight: normal; background: none;");
        m_lblGoteTurn->setStyleSheet(baseGote + " font-weight: bold; background: #e0e0e0;");
    } else {
        m_lblSenteTurn->setStyleSheet(baseSente);
        m_lblGoteTurn->setStyleSheet(baseGote);
    }
}

void UIController::onRestartClicked() {
    // 二次确认
    auto reply = QMessageBox::question(this, "重新开始", "确定重置对局吗？", QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        bool ok;
        int tMin = QInputDialog::getInt(this, "设置", "总时长(分钟):", 10, 1, 120, 1, &ok);
        if (!ok) return;
        int tInc = QInputDialog::getInt(this, "设置", "每步奖励(秒):", 10, 0, 60, 1, &ok);
        if (!ok) return;

        m_secondsElapsed = 0;
        m_txtHistory->clear();
        m_gameEngine->startGame(tMin * 60, tInc); // 调用引擎重启
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
        int result = (m_gameEngine->getCurrentPlayer() == Player::Sente) ? 2 : 1;
        // 调用引擎结束游戏
        m_gameEngine->finishGame(result);
    }
}
