#include "GameSetupDialog.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

GameSetupDialog::GameSetupDialog(QWidget* parent)
    : QDialog(parent),
      m_playerOneEdit(new QLineEdit("玩家一", this)),
      m_playerTwoEdit(new QLineEdit("玩家二", this)),
      m_totalMinutesSpin(new QSpinBox(this)),
      m_incrementSecondsSpin(new QSpinBox(this)),
      m_gameModeCombo(new QComboBox(this)),
      m_aiDepthSpin(new QSpinBox(this)),
      m_guessingPlayerCombo(new QComboBox(this)),
      m_oddButton(new QRadioButton("奇数", this)),
      m_evenButton(new QRadioButton("偶数", this)),
      m_resultLabel(new QLabel("设置完成后点击“开始猜先”。", this)),
      m_buttonBox(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                       this)) {
    setWindowTitle("对局设置与猜先");
    setModal(true);
    setMinimumWidth(420);

    m_totalMinutesSpin->setRange(1, 120);
    m_totalMinutesSpin->setValue(5);
    m_incrementSecondsSpin->setRange(0, 60);
    m_incrementSecondsSpin->setValue(5);
    m_gameModeCombo->addItems({"双人对战", "人机对战"});
    m_gameModeCombo->setObjectName("gameModeCombo");
    m_aiDepthSpin->setRange(1, 3);
    m_aiDepthSpin->setValue(3);
    m_aiDepthSpin->setEnabled(false);
    m_aiDepthSpin->setObjectName("aiDepthSpin");
    m_guessingPlayerCombo->addItems({"玩家一", "玩家二"});
    m_oddButton->setChecked(true);
    m_resultLabel->setWordWrap(true);

    auto* settingsLayout = new QFormLayout;
    settingsLayout->addRow("玩家一名称", m_playerOneEdit);
    settingsLayout->addRow("玩家二名称", m_playerTwoEdit);
    settingsLayout->addRow("总时长（分钟）", m_totalMinutesSpin);
    settingsLayout->addRow("每步奖励（秒）", m_incrementSecondsSpin);
    settingsLayout->addRow("对局模式", m_gameModeCombo);
    settingsLayout->addRow("AI 搜索深度", m_aiDepthSpin);

    auto* guessLayout = new QHBoxLayout;
    guessLayout->addWidget(m_guessingPlayerCombo);
    guessLayout->addWidget(m_oddButton);
    guessLayout->addWidget(m_evenButton);

    auto* guessGroup = new QGroupBox("猜先", this);
    guessGroup->setLayout(guessLayout);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(settingsLayout);
    mainLayout->addWidget(guessGroup);
    mainLayout->addWidget(m_resultLabel);
    mainLayout->addWidget(m_buttonBox);

    m_buttonBox->button(QDialogButtonBox::Ok)->setText("开始猜先");
    connect(m_buttonBox->button(QDialogButtonBox::Ok), &QPushButton::clicked,
            this, [this] { handlePrimaryAction(); });
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_gameModeCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        const bool versusAi = index == 1;
        m_aiDepthSpin->setEnabled(versusAi);
        m_guessingPlayerCombo->setCurrentIndex(0);
        m_guessingPlayerCombo->setEnabled(!versusAi);
        if (versusAi && m_playerTwoEdit->text().trimmed() == "玩家二") {
            m_playerTwoEdit->setText("AI");
        }
    });
}

void GameSetupDialog::handlePrimaryAction() {
    if (m_drawCompleted) {
        accept();
        return;
    }
    if (!validatePlayers()) return;

    const QString playerOne = m_playerOneEdit->text().trimmed();
    const QString playerTwo = m_playerTwoEdit->text().trimmed();
    const bool playerOneGuesses = m_guessingPlayerCombo->currentIndex() == 0;
    const QString guesser = playerOneGuesses ? playerOne : playerTwo;
    const QString other = playerOneGuesses ? playerTwo : playerOne;
    const ParityGuess guess = m_oddButton->isChecked()
                                  ? ParityGuess::Odd
                                  : ParityGuess::Even;

    m_result.openingDraw = OpeningDraw::draw(guess);
    m_result.guessingPlayerName = guesser;
    m_result.sentePlayerName = m_result.openingDraw.guessedCorrect ? guesser : other;
    m_result.gotePlayerName = m_result.openingDraw.guessedCorrect ? other : guesser;
    m_result.totalSeconds = m_totalMinutesSpin->value() * 60;
    m_result.incrementSeconds = m_incrementSecondsSpin->value();
    m_result.playAgainstAi = m_gameModeCombo->currentIndex() == 1;
    m_result.aiIsSente = m_result.playAgainstAi && m_result.sentePlayerName == playerTwo;
    m_result.aiDepth = m_aiDepthSpin->value();

    const QString parityText = guess == ParityGuess::Odd ? "奇数" : "偶数";
    const QString outcomeText = m_result.openingDraw.guessedCorrect ? "猜中" : "未猜中";
    m_resultLabel->setText(
        QString("生成数字：%1（%2）。%3猜%4，%5；%6执先。")
            .arg(m_result.openingDraw.number)
            .arg(m_result.openingDraw.number % 2 == 0 ? "偶数" : "奇数")
            .arg(guesser, parityText, outcomeText, m_result.sentePlayerName));

    // 猜先完成后锁定参数，避免显示结果与实际开局配置不一致。
    setSetupControlsEnabled(false);
    m_drawCompleted = true;
    m_buttonBox->button(QDialogButtonBox::Ok)->setText("开始对局");
}

bool GameSetupDialog::validatePlayers() {
    const QString playerOne = m_playerOneEdit->text().trimmed();
    const QString playerTwo = m_playerTwoEdit->text().trimmed();
    if (playerOne.isEmpty() || playerTwo.isEmpty()) {
        QMessageBox::warning(this, "对局设置", "玩家名称不能为空。");
        return false;
    }
    if (playerOne == playerTwo) {
        QMessageBox::warning(this, "对局设置", "两位玩家需要使用不同名称。");
        return false;
    }
    return true;
}

void GameSetupDialog::setSetupControlsEnabled(bool enabled) {
    m_playerOneEdit->setEnabled(enabled);
    m_playerTwoEdit->setEnabled(enabled);
    m_totalMinutesSpin->setEnabled(enabled);
    m_incrementSecondsSpin->setEnabled(enabled);
    m_gameModeCombo->setEnabled(enabled);
    m_aiDepthSpin->setEnabled(enabled && m_gameModeCombo->currentIndex() == 1);
    m_guessingPlayerCombo->setEnabled(enabled);
    m_oddButton->setEnabled(enabled);
    m_evenButton->setEnabled(enabled);
}
