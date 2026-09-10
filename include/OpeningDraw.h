#pragma once

enum class ParityGuess {
    Odd,
    Even
};

struct OpeningDrawResult {
    int number = 0;
    ParityGuess guess = ParityGuess::Odd;
    bool guessedCorrect = false;
};

class OpeningDraw {
public:
    // 生成 1~100 的整数，并按玩家选择的奇偶完成猜先。
    static OpeningDrawResult draw(ParityGuess guess);
    static bool matches(int number, ParityGuess guess);
};
