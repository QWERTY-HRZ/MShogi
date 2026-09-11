#include "UIController.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QMessageBox>
#include "GameSetupDialog.h"
// QSS
#include <QStyle>

UIController::UIController(QWidget *parent, bool promptOnStart)
    : QMainWindow(parent)
{
    m_bgPixmap.load(":/res/bg_board.png");
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
    connect(m_gameEngine, &GameEngine::kingThreatStatusChanged,
            this, &UIController::onKingThreatStatusChanged);
    connect(m_gameEngine, &GameEngine::undoExecuted, this, &UIController::onUndoExecuted);
    connect(m_gameEngine->getClock(), &ChessClock::timeUpdated, this, &UIController::onUpdateTimer);

    if (promptOnStart) {
        QTimer::singleShot(0, this, [this] { promptSettingsAndStart(); });
    }
}

bool UIController::handleMoveRequest(const Move& move) {
    return m_gameEngine->makeMove(move);
}

void UIController::setupUi() {
    QWidget* centralWidget = new QWidget(this);
    // 便于 QSS 识别
    centralWidget->setObjectName("centralWidget");
    setCentralWidget(centralWidget);
    QHBoxLayout* mainLayout = new QHBoxLayout(centralWidget);
    // 整体窗口
    m_view = new QGraphicsView(m_scene);
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setDragMode(QGraphicsView::NoDrag);
    m_view->setMinimumSize(100, 100);
    // 去掉滚动条
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    mainLayout->addWidget(m_view, 3);

    QVBoxLayout* sideLayout = new QVBoxLayout();
    QGroupBox* statusGroup = new QGroupBox("对局信息");
    QVBoxLayout* statusLayout = new QVBoxLayout(statusGroup);
    // 重构状态栏 加上 setObjectName
    m_lblStatus = new QLabel("状态: 初始化");
    m_lblStatus->setObjectName("lblStatus");
    m_lblStatus->setWordWrap(true);

    m_lblOpeningDraw = new QLabel("猜先数字：--");
    m_lblOpeningDraw->setObjectName("lblOpeningDraw");

    statusLayout->addWidget(m_lblStatus);
    statusLayout->addWidget(m_lblOpeningDraw);
    sideLayout->addWidget(statusGroup);

    QGroupBox* clockGroup = new QGroupBox("棋钟");
    QVBoxLayout* clockLayout = new QVBoxLayout(clockGroup);

    m_lblGameInfo = new QLabel("总用时: 00:00 | 棋钟: -- 分 + --");
    m_lblGameInfo->setObjectName("lblGameInfo");

    m_lblSenteTurn = new QLabel("先手回合：--");
    m_lblSenteTurn->setObjectName("lblSenteTurn");

    m_lblGoteTurn = new QLabel("后手回合：--");
    m_lblGoteTurn->setObjectName("lblGoteTurn");

    // 对局信息保持精简，所有计时内容统一放入独立棋钟区域。
    clockLayout->addWidget(m_lblGameInfo);
    clockLayout->addWidget(m_lblSenteTurn);
    clockLayout->addWidget(m_lblGoteTurn);
    sideLayout->addWidget(clockGroup);
    // 行棋记录
    QGroupBox* historyGroup = new QGroupBox("棋谱");
    QVBoxLayout* historyLayout = new QVBoxLayout(historyGroup);

    m_txtHistory = new QTextEdit();
    m_txtHistory->setObjectName("txtHistory");
    m_txtHistory->setReadOnly(true);
    m_txtHistory->setMinimumSize(100, 100);
    historyLayout->addWidget(m_txtHistory);
    // 添加参数 1，让棋谱区主动承担高度伸缩，以显示底部按钮
    sideLayout->addWidget(historyGroup, 1);

    // 按钮布局
    QGridLayout* btnLayout = new QGridLayout();
    btnLayout->setSpacing(10);
    // 顶部留出一点边距，与记谱区隔开
    btnLayout->setContentsMargins(0, 10, 0, 0);
    // 定义统一的图标大小
    QSize iconSize(24, 24);

    // 按钮
    m_btnUndo = new QPushButton(" 悔棋");
    m_btnUndo->setIcon(QIcon(":/res/icons/btn_undo.svg"));
    m_btnUndo->setIconSize(iconSize);
    connect(m_btnUndo, &QPushButton::clicked, m_gameEngine, &GameEngine::undo);
    m_btnUndo->setEnabled(false);
    btnLayout->addWidget(m_btnUndo, 0, 0);

    m_btnRestart = new QPushButton(" 重开");
    m_btnRestart->setIcon(QIcon(":/res/icons/btn_restart.svg"));
    m_btnRestart->setIconSize(iconSize);
    connect(m_btnRestart, &QPushButton::clicked, this, &UIController::onRestartClicked);
    btnLayout->addWidget(m_btnRestart, 0, 1);

    m_btnResign = new QPushButton(" 认输");
    m_btnResign->setIcon(QIcon(":/res/icons/btn_resign.svg"));
    m_btnResign->setIconSize(iconSize);
    connect(m_btnResign, &QPushButton::clicked, this, &UIController::onResignClicked);
    btnLayout->addWidget(m_btnResign, 1, 0);

    m_btnPauseResume = new QPushButton(" 暂停");
    m_btnPauseResume->setIcon(QIcon(":/res/icons/btn_pause.svg"));
    m_btnPauseResume->setIconSize(iconSize);
    connect(m_btnPauseResume, &QPushButton::clicked, this, &UIController::onPauseResumeClicked);
    btnLayout->addWidget(m_btnPauseResume, 1, 1);

    sideLayout->addLayout(btnLayout);
    mainLayout->addLayout(sideLayout, 1);

    this->setMinimumSize(800, 600);
    resize(GameConstants::INITIAL_WIDTH, GameConstants::INITIAL_HEIGHT);
}

bool UIController::promptSettingsAndStart() {
    GameSetupDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return false;

    const GameSetupResult& setup = dialog.setupResult();
    m_sentePlayerName = setup.sentePlayerName;
    m_gotePlayerName = setup.gotePlayerName;
    m_lblOpeningDraw->setText(QString("猜先数字：%1").arg(setup.openingDraw.number));
    m_checkNotice.clear();

    // 所有开局参数在同一对话框确认后，再一次性重置并启动对局。
    m_txtHistory->clear();
    m_gameEngine->clearAgents();
    if (setup.playAgainstAi) {
        const Player aiPlayer = setup.aiIsSente ? Player::Sente : Player::Gote;
        m_gameEngine->setAgent(aiPlayer,
                               std::make_shared<AlphaBetaAgent>(setup.aiDepth));
    }
    m_gameEngine->startGame(setup.totalSeconds, setup.incrementSeconds);
    return true;
}

void UIController::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    // 让棋盘内容自适应 View 的大小
    if (m_view && m_scene) {
        m_view->fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
    }
    // 重新启用动态计算字体缩放因子 (以初始宽高为基准)
    qreal scaleW = (qreal)this->width() / GameConstants::INITIAL_WIDTH;
    qreal scaleH = (qreal)this->height() / GameConstants::INITIAL_HEIGHT;
    qreal scale = qBound(0.6, qMin(scaleW, scaleH), 3.0);

    // 动态更新所有文字控件的像素大小
    auto setScaledFont = [&](QWidget* w, int baseSize, bool bold = false) {
        if (!w) return;
        QFont f = w->font();
        f.setPixelSize(qRound(baseSize * scale));
        f.setBold(bold);
        w->setFont(f);
    };

    // 状态标签
    setScaledFont(m_lblStatus, 22, true);
    setScaledFont(m_lblOpeningDraw, 20);
    setScaledFont(m_lblGameInfo, 20);
    setScaledFont(m_lblSenteTurn, 20);
    setScaledFont(m_lblGoteTurn, 20);
    setScaledFont(m_txtHistory, 20, true);
    // 按钮变量名更新
    setScaledFont(m_btnUndo, 18, true);
    setScaledFont(m_btnRestart, 18, true);
    setScaledFont(m_btnResign, 18, true);
    setScaledFont(m_btnPauseResume, 18, true);

    // 直接使用现成的 lambda 表达式，安全地修改 QGroupBox 本身
    QList<QGroupBox*> groups = this->findChildren<QGroupBox*>();
    for (auto group : groups) {
        setScaledFont(group, 20, true);
    }
}

void UIController::onStateChanged(GameState newState) {
    scheduleBoardRefresh();
    switch (newState) {
        case GameState::Playing:
            m_lblStatus->setText(m_gameEngine->isAgentTurn()
                                     ? "状态: AI 思考中"
                                     : QString("状态: 对局中%1").arg(m_checkNotice));
            m_btnPauseResume->setText(" 暂停");
            m_btnPauseResume->setIcon(QIcon(":/res/icons/btn_pause.svg"));
            m_btnPauseResume->setEnabled(true);
            m_btnUndo->setEnabled(m_gameEngine->getHistory().canUndo());
            m_btnResign->setEnabled(true);
            break;
        case GameState::Paused:
            m_lblStatus->setText(QString("状态: 已暂停%1").arg(m_checkNotice));
            m_btnPauseResume->setIcon(QIcon(":/res/icons/btn_play.svg"));
            m_btnPauseResume->setText(" 继续");
            m_btnUndo->setEnabled(m_gameEngine->getHistory().canUndo());
            m_btnResign->setEnabled(false);
            break;
        case GameState::End:
            m_checkNotice.clear();
            m_lblStatus->setText("状态: 结束");
            m_btnPauseResume->setEnabled(false);
            m_btnUndo->setEnabled(false);
            m_btnResign->setEnabled(false);
            break;
        default: break;
    }
    onUpdateTimer();
}

// 状态修改放到 onUpdateTimer
void UIController::onMoveExecuted(const std::string& notation) {
    m_txtHistory->append(QString::fromStdString(notation));
    m_btnUndo->setEnabled(true);
    m_lblStatus->setText(m_gameEngine->isAgentTurn()
                             ? "状态: AI 思考中"
                             : QString("状态: 对局中%1").arg(m_checkNotice));
    scheduleBoardRefresh();
}

void UIController::onGameEnded(int result, GameEndReason reason) {
    if (reason == GameEndReason::RepetitionDraw) {
        QMessageBox::information(this, "对局结束", "本局和棋！\n\n结束原因：三次重复局面");
        return;
    }
    const QString winnerRole = result == 1 ? "先手" : "后手";
    const QString winnerName = result == 1 ? m_sentePlayerName : m_gotePlayerName;
    QString reasonText;
    switch (reason) {
        case GameEndReason::KingCaptured: reasonText = "实际吃掉对方王"; break;
        case GameEndReason::BaselineEntry: reasonText = "王下底后存活一回合"; break;
        case GameEndReason::NoLegalAction: reasonText = "对方无合法着法"; break;
        case GameEndReason::RepetitionDraw: break;
        case GameEndReason::Timeout: reasonText = "对方棋钟归零"; break;
        case GameEndReason::Resignation: reasonText = "对方认输"; break;
    }
    const QString msg = QString("%1（%2）获胜！\n\n结束原因：%3")
                            .arg(winnerRole, winnerName, reasonText);
    QMessageBox::information(this, "对局结束", msg);
}

void UIController::onKingThreatStatusChanged(bool senteThreatened,
                                             bool goteThreatened) {
    if (senteThreatened && goteThreatened) {
        m_checkNotice = QString(" | 将军：先手（%1）与后手（%2）均受威胁")
                            .arg(m_sentePlayerName, m_gotePlayerName);
    } else if (senteThreatened) {
        m_checkNotice = QString(" | 将军：先手（%1）受威胁").arg(m_sentePlayerName);
    } else if (goteThreatened) {
        m_checkNotice = QString(" | 将军：后手（%1）受威胁").arg(m_gotePlayerName);
    } else {
        m_checkNotice.clear();
    }

    if (m_gameEngine->getCurrentState() == GameState::Playing) {
        m_lblStatus->setText(m_gameEngine->isAgentTurn()
                                 ? "状态: AI 思考中"
                                 : QString("状态: 对局中%1").arg(m_checkNotice));
    } else if (m_gameEngine->getCurrentState() == GameState::Paused) {
        m_lblStatus->setText(QString("状态: 已暂停%1").arg(m_checkNotice));
    }
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
    m_lblSenteTurn->setText(QString("先手（%1）：剩余 %2:%3")
                            .arg(m_sentePlayerName)
                            .arg(sTime / 60, 2, 10, QChar('0')).arg(sTime % 60, 2, 10, QChar('0')));
    m_lblGoteTurn->setText(QString("后手（%1）：剩余 %2:%3")
                           .arg(m_gotePlayerName)
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

    // 重新渲染
    m_lblSenteTurn->style()->unpolish(m_lblSenteTurn);
    m_lblSenteTurn->style()->polish(m_lblSenteTurn);
    m_lblGoteTurn->style()->unpolish(m_lblGoteTurn);
    m_lblGoteTurn->style()->polish(m_lblGoteTurn);
}

void UIController::onUndoExecuted() {
    refreshHistory();
    m_btnUndo->setEnabled(m_gameEngine->getHistory().canUndo());
    scheduleBoardRefresh();
    onUpdateTimer();
}

void UIController::scheduleBoardRefresh() {
    // 延迟到当前鼠标事件结束后刷新，避免 clear() 提前删除正在回调的棋子。
    QTimer::singleShot(0, m_scene, &GameScene::refreshBoard);
}

void UIController::refreshHistory() {
    m_txtHistory->clear();
    for (const auto& node : m_gameEngine->getHistory().getHistory()) {
        m_txtHistory->append(QString::fromStdString(node.notation));
    }
}

void UIController::onRestartClicked() {
    if (m_gameEngine->getCurrentState() == GameState::Init) {
        promptSettingsAndStart();
        return;
    }

    Player curP = m_gameEngine->getCurrentPlayer();
    const QString playerStr = curP == Player::Sente
                                  ? QString("先手（%1）").arg(m_sentePlayerName)
                                  : QString("后手（%1）").arg(m_gotePlayerName);
    QString msg = QString("当前是【%1】回合。\n\n确定要重置对局吗？").arg(playerStr);

    auto reply = QMessageBox::question(this, "重新开始", msg, QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        const GameState previousState = m_gameEngine->getCurrentState();
        if (previousState == GameState::Playing) m_gameEngine->pauseGame();
        if (!promptSettingsAndStart() && previousState == GameState::Playing) {
            m_gameEngine->resumeGame();
        }
    }
}

void UIController::onResignClicked() {
    if (m_gameEngine->getCurrentState() != GameState::Playing) return;
    Player curP = m_gameEngine->getCurrentPlayer();
    const QString playerStr = curP == Player::Sente
                                  ? QString("先手（%1）").arg(m_sentePlayerName)
                                  : QString("后手（%1）").arg(m_gotePlayerName);
    QString msg = QString("当前是【%1】回合。\n\n确定要投降吗？").arg(playerStr);

    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "投降", msg, QMessageBox::Yes | QMessageBox::No);
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

void UIController::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    if (!m_bgPixmap.isNull()) {
        // drawPixmap 会自动将图片平滑拉伸铺满 rect() (即整个窗口)
        painter.drawPixmap(rect(), m_bgPixmap);
    }
    QMainWindow::paintEvent(event);
}
