#include "../include/UIController.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QInputDialog>
// QSS
#include <QStyle>

UIController::UIController(QWidget *parent)
    : QMainWindow(parent)
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

    // UI 渲染完后，设置游戏时长
    QTimer::singleShot(0, this, &UIController::promptSettingsAndStart);
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
    // 重构状态栏 加上 setObjectName
    m_lblStatus = new QLabel("状态: 初始化");
    m_lblStatus->setObjectName("lblStatus");

    m_lblGameInfo = new QLabel("总用时: 00:00 | 棋钟: -- 分 + --");
    m_lblGameInfo->setObjectName("lblGameInfo");

    m_lblSenteTurn = new QLabel("先手回合：--");
    m_lblSenteTurn->setObjectName("lblSenteTurn");

    m_lblGoteTurn = new QLabel("后手回合：--");
    m_lblGoteTurn->setObjectName("lblGoteTurn");
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
    m_txtHistory->setObjectName("txtHistory");
    m_txtHistory->setReadOnly(true);

    historyLayout->addWidget(m_txtHistory);
    sideLayout->addWidget(historyGroup);
    // 按钮
    QPushButton* btnUndo = new QPushButton(" 悔棋");
    btnUndo->setIcon(QIcon(":res/icons/btn_undo.svg"));
    connect(btnUndo, &QPushButton::clicked, m_gameEngine, &GameEngine::undo);
    sideLayout->addWidget(btnUndo);

    QPushButton* btnRestart = new QPushButton(" 重开");
    btnRestart->setIcon(QIcon(":res/icons/btn_restart.svg"));
    connect(btnRestart, &QPushButton::clicked, this, &UIController::onRestartClicked);
    sideLayout->addWidget(btnRestart);

    QPushButton* btnResign = new QPushButton(" 认输");
    btnResign->setIcon(QIcon(":res/icons/btn_resign.svg"));
    connect(btnResign, &QPushButton::clicked, this, &UIController::onResignClicked);
    sideLayout->addWidget(btnResign);

    m_btnPauseResume = new QPushButton(" 暂停");
    m_btnPauseResume->setIcon(QIcon(":res/icons/btn_pause.svg"));
    connect(m_btnPauseResume, &QPushButton::clicked, this, &UIController::onPauseResumeClicked);
    sideLayout->addWidget(m_btnPauseResume);

    mainLayout->addLayout(sideLayout, 1);
    resize(1280, 960);
}

void UIController::promptSettingsAndStart() {
    // 开始游戏 设置时间
    bool ok;
    int tMin = QInputDialog::getInt(this, "游戏设置", "总时长(分钟，默认为 5):", 5, 1, 120, 1, &ok);
    if (!ok) tMin = 5;
    int tInc = QInputDialog::getInt(this, "游戏设置", "每步奖励(秒，默认为 5):", 5, 0, 60, 1, &ok);
    if (!ok) tInc = 5;

    m_txtHistory->clear();
    m_gameEngine->startGame(tMin * 60, tInc);
}

void UIController::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (m_view && m_scene) m_view->fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
}

void UIController::onStateChanged(GameState newState) {
    // 不在 UI 中操控定时
    m_scene->refreshBoard();
    switch (newState) {
        case GameState::Playing:
            m_lblStatus->setText("状态: 对局中");
            m_btnPauseResume->setText(" 暂停");
            m_btnPauseResume->setIcon(QIcon(":/res/icons/btn_pause.svg"));
            m_btnPauseResume->setEnabled(true);
            break;
        case GameState::Paused:
            m_lblStatus->setText("状态: 已暂停");
            m_btnPauseResume->setIcon(QIcon(":/res/icons/btn_play.svg"));
            m_btnPauseResume->setText(" 继续");
            break;
        case GameState::End:
            m_lblStatus->setText("状态: 结束");
            m_btnPauseResume->setEnabled(false);
            break;
        default: break;
    }
    onUpdateTimer();
}

// 状态修改放到 onUpdateTimer
void UIController::onMoveExecuted(const std::string& notation) {
    m_txtHistory->append(QString::fromStdString(notation));
    m_scene->refreshBoard();
}

void UIController::onGameEnded(int result) {
    QString msg = (result == 1) ? "先手获胜！" : "后手获胜！";
    QMessageBox::information(this, "对局结束", msg);
}

void UIController::onUpdateTimer() {
    // 直接从 GameEngine 中拉取数据，UI 不再参与数据计算！
    int elapsed = m_gameEngine->getTotalSecondsElapsed();
    int totalM = elapsed / 60;
    int totalS = elapsed % 60;

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

    // 使用动态属性 替代硬编码的 StyleSheet
    if (isPlaying && curP == Player::Sente) {
        m_lblSenteTurn->setProperty("isActive", true);
        m_lblGoteTurn->setProperty("isActive", false);
    } else if (isPlaying && curP == Player::Gote) {
        m_lblSenteTurn->setProperty("isActive", false);
        m_lblGoteTurn->setProperty("isActive", true);
    } else {
        m_lblSenteTurn->setProperty("isActive", false);
        m_lblGoteTurn->setProperty("isActive", false);
    }

    // 强制刷新样式以应用 QSS
    m_lblSenteTurn->style()->unpolish(m_lblSenteTurn);
    m_lblSenteTurn->style()->polish(m_lblSenteTurn);
    m_lblGoteTurn->style()->unpolish(m_lblGoteTurn);
    m_lblGoteTurn->style()->polish(m_lblGoteTurn);
}

void UIController::onUndoExecuted() {
    // 状态修改放到 onUpdateTimer
    m_scene->refreshBoard();
    QTextCursor cursor = m_txtHistory->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.select(QTextCursor::BlockUnderCursor);
    cursor.removeSelectedText();
    cursor.deleteChar();
}

void UIController::onRestartClicked() {
    auto reply = QMessageBox::question(this, "重新开始", "确定重置对局吗？", QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        // 统一重新开始
        promptSettingsAndStart();
    }
}

void UIController::onResignClicked() {
    if (m_gameEngine->getCurrentState() != GameState::Playing) return;

    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "认输", "确定要投降吗？", QMessageBox::Yes | QMessageBox::No);
    // 由 GameEngine 统一处理胜负
    if (reply == QMessageBox::Yes) m_gameEngine->resign();
}

void UIController::onPauseResumeClicked() {
    GameState state = m_gameEngine->getCurrentState();
    if (state == GameState::Playing) {
        m_gameEngine->pauseGame();
    } else if (state == GameState::Paused) {
        m_gameEngine->resumeGame();
    }
}
