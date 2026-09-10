#include <gtest/gtest.h>
#include "OpeningDraw.h"

TEST(OpeningDrawTest, MatchesOddAndEvenExactly) {
    EXPECT_TRUE(OpeningDraw::matches(1, ParityGuess::Odd));
    EXPECT_FALSE(OpeningDraw::matches(1, ParityGuess::Even));
    EXPECT_TRUE(OpeningDraw::matches(100, ParityGuess::Even));
    EXPECT_FALSE(OpeningDraw::matches(100, ParityGuess::Odd));
}

TEST(OpeningDrawTest, GeneratedNumberStaysInConfiguredRange) {
    for (int i = 0; i < 100; ++i) {
        const OpeningDrawResult result = OpeningDraw::draw(ParityGuess::Odd);
        EXPECT_GE(result.number, 1);
        EXPECT_LE(result.number, 100);
        EXPECT_EQ(result.guessedCorrect, result.number % 2 != 0);
    }
}
