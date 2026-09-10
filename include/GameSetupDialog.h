#pragma once

#include <QDialog>
#include <QString>
#include "OpeningDraw.h"

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QRadioButton;
class QSpinBox;

struct GameSetupResult {
    QString sentePlayerName;
    QString gotePlayerName;
    QString guessingPlayerName;
    int totalSeconds = 300;
    int incrementSeconds = 5;
    bool playAgainstAi = false;
    bool aiIsSente = false;
    int aiDepth = 3;
    OpeningDrawResult openingDraw;
};

class GameSetupDialog : public QDialog {
public:
    explicit GameSetupDialog(QWidget* parent = nullptr);

    const GameSetupResult& setupResult() const { return m_result; }

private:
    void handlePrimaryAction();
    bool validatePlayers();
    void setSetupControlsEnabled(bool enabled);

    QLineEdit* m_playerOneEdit;
    QLineEdit* m_playerTwoEdit;
    QSpinBox* m_totalMinutesSpin;
    QSpinBox* m_incrementSecondsSpin;
    QComboBox* m_gameModeCombo;
    QSpinBox* m_aiDepthSpin;
    QComboBox* m_guessingPlayerCombo;
    QRadioButton* m_oddButton;
    QRadioButton* m_evenButton;
    QLabel* m_resultLabel;
    QDialogButtonBox* m_buttonBox;
    bool m_drawCompleted = false;
    GameSetupResult m_result;
};
