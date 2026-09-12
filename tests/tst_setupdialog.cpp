#include <gtest/gtest.h>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QPushButton>
#include <QTest>
#include "GameSetupDialog.h"

TEST(GameSetupDialogTest, DrawsBeforeAcceptingIntegratedSettings) {
    GameSetupDialog dialog;
    dialog.show();

    auto* buttonBox = dialog.findChild<QDialogButtonBox*>();
    ASSERT_NE(buttonBox, nullptr);
    QPushButton* primaryButton = buttonBox->button(QDialogButtonBox::Ok);
    ASSERT_NE(primaryButton, nullptr);
    EXPECT_EQ(primaryButton->text(), "开始猜先");

    QTest::mouseClick(primaryButton, Qt::LeftButton);
    const GameSetupResult& setup = dialog.setupResult();
    EXPECT_GE(setup.openingDraw.number, 1);
    EXPECT_LE(setup.openingDraw.number, 100);
    EXPECT_FALSE(setup.sentePlayerName.isEmpty());
    EXPECT_FALSE(setup.gotePlayerName.isEmpty());
    EXPECT_EQ(primaryButton->text(), "开始对局");

    QTest::mouseClick(primaryButton, Qt::LeftButton);
    EXPECT_EQ(dialog.result(), QDialog::Accepted);
}

TEST(GameSetupDialogTest, HumanVersusAiSettingsAssignTheAiByDrawResult) {
    GameSetupDialog dialog;
    auto* modeCombo = dialog.findChild<QComboBox*>("gameModeCombo");
    auto* buttonBox = dialog.findChild<QDialogButtonBox*>();
    ASSERT_NE(modeCombo, nullptr);
    ASSERT_NE(buttonBox, nullptr);
    modeCombo->setCurrentIndex(1);

    QTest::mouseClick(buttonBox->button(QDialogButtonBox::Ok), Qt::LeftButton);
    const GameSetupResult& setup = dialog.setupResult();
    EXPECT_TRUE(setup.playAgainstAi);
    EXPECT_EQ(setup.aiIsSente, setup.sentePlayerName == "AI");
    EXPECT_GE(setup.aiDepth, 1);
    EXPECT_LE(setup.aiDepth, 3);
#ifdef MSHOGI_WITH_ONNX_AGENT
    EXPECT_TRUE(setup.useNeuralAi);
#else
    EXPECT_FALSE(setup.useNeuralAi);
#endif
}
