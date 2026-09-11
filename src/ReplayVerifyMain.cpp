#include "SelfPlay.h"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const std::size_t separator = line.find('\t', start);
        fields.push_back(line.substr(start, separator - start));
        if (separator == std::string::npos) return fields;
        start = separator + 1;
    }
}

Player parsePlayer(const std::string& value) {
    if (value == "S") return Player::Sente;
    if (value == "G") return Player::Gote;
    throw std::invalid_argument("player must be S or G");
}

const char* endReasonName(CoreEndReason reason) {
    switch (reason) {
        case CoreEndReason::KingCaptured: return "king_captured";
        case CoreEndReason::BaselineEntry: return "baseline_entry";
        case CoreEndReason::NoLegalAction: return "no_legal_action";
        case CoreEndReason::RepetitionDraw: return "threefold_repetition";
        case CoreEndReason::None: return "none";
    }
    return "none";
}

[[noreturn]] void fail(std::size_t lineNumber, const std::string& message) {
    throw std::runtime_error("protocol line " + std::to_string(lineNumber) +
                             ": " + message);
}
}

int main() {
    try {
        GameCore core;
        bool inGame = false;
        int gameId = -1;
        int expectedPly = 0;
        int games = 0;
        std::uint64_t positions = 0;
        std::string line;
        std::size_t lineNumber = 0;

        while (std::getline(std::cin, line)) {
            ++lineNumber;
            if (line.empty()) continue;
            const auto fields = splitTabs(line);
            if (fields[0] == "G") {
                if (fields.size() != 2 || inGame) fail(lineNumber, "invalid game start");
                gameId = std::stoi(fields[1]);
                expectedPly = 0;
                core.reset();
                inGame = true;
                continue;
            }

            if (fields[0] == "P") {
                if (fields.size() != 6 || !inGame) fail(lineNumber, "invalid position");
                if (std::stoi(fields[1]) != gameId) fail(lineNumber, "game id mismatch");
                if (std::stoi(fields[2]) != expectedPly) fail(lineNumber, "ply mismatch");
                const Player player = parsePlayer(fields[3]);
                if (player != core.currentPlayer()) fail(lineNumber, "player mismatch");
                if (fields[5] != core.serializeState()) fail(lineNumber, "state mismatch");

                const Move move = decodeAction(std::stoi(fields[4]), player);
                if (!core.isLegalAction(move) || !core.applyAction(move)) {
                    fail(lineNumber, "selected action is illegal");
                }
                ++expectedPly;
                ++positions;
                continue;
            }

            if (fields[0] == "E") {
                if (fields.size() != 6 || !inGame) fail(lineNumber, "invalid game end");
                if (std::stoi(fields[1]) != gameId) fail(lineNumber, "game id mismatch");
                if (std::stoi(fields[2]) != expectedPly) fail(lineNumber, "ply count mismatch");
                const int winner = std::stoi(fields[3]);
                const std::string& reason = fields[4];
                const bool truncated = fields[5] == "1";

                if (truncated) {
                    if (core.isTerminal() || winner != 0 || reason != "ply_limit") {
                        fail(lineNumber, "invalid truncated result");
                    }
                } else if (!core.isTerminal() || core.winner() != winner ||
                           reason != endReasonName(core.endReason())) {
                    fail(lineNumber, "terminal result mismatch");
                }
                inGame = false;
                ++games;
                continue;
            }

            fail(lineNumber, "unknown record type");
        }

        if (inGame) fail(lineNumber, "unterminated game");
        std::cout << "games=" << games << " positions=" << positions << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "mshogi_replay_verify: " << error.what() << '\n';
        return 2;
    }
}
