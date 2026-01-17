#include "../include/UIController.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QMessageBox>

UIController::UIController(QWidget *parent) : QMainWindow(parent), m_secondsElapsed(0) {
    m_gameEngine = new GameEngine(this);
    m_scene = new GameScene(m_gameEngine, this);

    // 注入回调：当 Scene 需要移动棋子时，调用 UIController 的 handleMoveRequest
    m_scene->setMoveRequestCallback([this](const Move& m) {
        return this->handleMoveRequest(m);
    });

    setupUi();

    connect(m_gameEngine, &GameEngine::stateChanged, this, &UIController::onStateChanged);
    connect(m_gameEngine, &GameEngine::moveExecuted, this, &UIController::onMoveExecuted);
    connect(m_gameEngine, &GameEngine::gameEnded, this, &UIController::onGameEnded);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &UIController::updateTimer);

    m_gameEngine->startGame();
}

bool UIController::handleMoveRequest(const Move& move) {
    // 这里是 UI 控制逻辑的核心，未来可以在此添加 UI 层的校验（如是否轮到玩家操作等）
    // 目前直接透传给引擎
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
            m_lblStatus->setText("状态: 对局中");
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
