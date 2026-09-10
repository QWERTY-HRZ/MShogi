#include <gtest/gtest.h>
#include <QDialogButtonBox>
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
