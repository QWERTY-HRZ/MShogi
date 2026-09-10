#include "OpeningDraw.h"
#include <QRandomGenerator>

OpeningDrawResult OpeningDraw::draw(ParityGuess guess) {
    const int number = QRandomGenerator::global()->bounded(1, 101);
    return {number, guess, matches(number, guess)};
}

bool OpeningDraw::matches(int number, ParityGuess guess) {
    const bool isOdd = number % 2 != 0;
    return guess == ParityGuess::Odd ? isOdd : !isOdd;
}
