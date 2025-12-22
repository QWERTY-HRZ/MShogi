#include <gtest/gtest.h>
#include "GameEngine.h"

TEST(GameEngineTest, InitialStateIsInit) {
    GameEngine engine;
    EXPECT_EQ(engine.getCurrentState(), GameState::Init);
}

TEST(GameEngineTest, StartGameTransitionsToPlaying) {
    GameEngine engine;
    engine.startGame();
    EXPECT_EQ(engine.getCurrentState(), GameState::Playing);
}