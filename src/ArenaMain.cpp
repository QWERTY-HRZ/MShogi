#include "SelfPlay.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifndef MSHOGI_CORE_COMMIT
#define MSHOGI_CORE_COMMIT "unknown"
#endif

namespace {
struct ArenaGame {
    GameCore core;
    bool neuralSente = false;
    int pairId = 0;
    int plies = 0;
    bool reported = false;
};

struct Config {
    int games = 1000;
    int alphaBetaDepth = 1;
    int openingPlies = 6;
    int maxPlies = 200;
    int threads = 1;
    std::uint64_t seed = 1;
};

std::string requireValue(int& index, int argc, char* argv[]) {
    if (++index >= argc) throw std::invalid_argument("missing option value");
    return argv[index];
}

std::uint64_t mixedSeed(std::uint64_t seed, int pairId, int ply) {
    std::uint64_t value = seed ^ (static_cast<std::uint64_t>(pairId + 1) *
                                  0x9E3779B97F4A7C15ULL);
    value ^= static_cast<std::uint64_t>(ply + 1) * 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 30;
    value *= 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 27;
    value *= 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
}

const char* playerName(Player player) {
    return player == Player::Sente ? "S" : "G";
}

const char* reasonName(CoreEndReason reason, bool truncated) {
    if (truncated) return "ply_limit";
    switch (reason) {
        case CoreEndReason::KingCaptured: return "king_captured";
        case CoreEndReason::BaselineEntry: return "baseline_entry";
        case CoreEndReason::NoLegalAction: return "no_legal_action";
        case CoreEndReason::RepetitionDraw: return "threefold_repetition";
        case CoreEndReason::None: return "none";
    }
    return "none";
}

bool neuralTurn(const ArenaGame& game) {
    return (game.core.currentPlayer() == Player::Sente) == game.neuralSente;
}

bool finished(const ArenaGame& game, int maxPlies) {
    return game.core.isTerminal() || game.plies >= maxPlies;
}

void applyOpening(ArenaGame& first, ArenaGame& second, int openingPlies,
                  std::uint64_t seed, int pairId) {
    AlphaBetaAgent openingAgent(2);
    for (int ply = 0; ply < openingPlies && !first.core.isTerminal(); ++ply) {
        const auto scored = openingAgent.scoreActions(first.core);
        const auto selected = AlphaBetaAgent::selectAction(
            scored, 1.0, mixedSeed(seed, pairId, ply));
        if (!selected || !first.core.applyAction(*selected) ||
            !second.core.applyAction(*selected)) {
            throw std::runtime_error("failed to create paired opening");
        }
        ++first.plies;
        ++second.plies;
    }
}

void reportFinished(std::vector<ArenaGame>& games, int maxPlies) {
    for (std::size_t index = 0; index < games.size(); ++index) {
        auto& game = games[index];
        if (game.reported || !finished(game, maxPlies)) continue;
        const bool truncated = !game.core.isTerminal();
        std::cout << "R\t" << index << '\t' << game.pairId << '\t'
                  << (game.neuralSente ? 'S' : 'G') << '\t'
                  << game.core.winner() << '\t'
                  << reasonName(game.core.endReason(), truncated) << '\t'
                  << game.plies << '\t' << (truncated ? 1 : 0) << '\n';
        game.reported = true;
    }
    std::cout.flush();
}
}

int main(int argc, char* argv[]) {
    try {
        Config config;
        config.threads = std::max(1u, std::thread::hardware_concurrency());
        for (int index = 1; index < argc; ++index) {
            const std::string option = argv[index];
            if (option == "--games") config.games = std::stoi(requireValue(index, argc, argv));
            else if (option == "--depth") config.alphaBetaDepth = std::stoi(requireValue(index, argc, argv));
            else if (option == "--opening-plies") config.openingPlies = std::stoi(requireValue(index, argc, argv));
            else if (option == "--max-plies") config.maxPlies = std::stoi(requireValue(index, argc, argv));
            else if (option == "--threads") config.threads = std::stoi(requireValue(index, argc, argv));
            else if (option == "--seed") config.seed = std::stoull(requireValue(index, argc, argv));
            else throw std::invalid_argument("unknown option: " + option);
        }
        if (config.games <= 0 || config.games % 2 != 0 || config.alphaBetaDepth <= 0 ||
            config.openingPlies < 0 || config.maxPlies <= config.openingPlies ||
            config.threads <= 0) {
            throw std::invalid_argument("invalid arena configuration");
        }

        std::cout << "M\t" << MSHOGI_CORE_COMMIT << '\t'
                  << GameConstants::RULE_VERSION << '\n';
        std::cout.flush();

        std::vector<ArenaGame> games(config.games);
        for (int pairId = 0; pairId < config.games / 2; ++pairId) {
            ArenaGame& neuralSente = games[pairId * 2];
            ArenaGame& neuralGote = games[pairId * 2 + 1];
            neuralSente.neuralSente = true;
            neuralGote.neuralSente = false;
            neuralSente.pairId = neuralGote.pairId = pairId;
            applyOpening(neuralSente, neuralGote, config.openingPlies,
                         config.seed, pairId);
        }

        AlphaBetaAgent alphaBeta(config.alphaBetaDepth);
        while (true) {
            reportFinished(games, config.maxPlies);
            if (std::all_of(games.begin(), games.end(), [](const ArenaGame& game) {
                    return game.reported;
                })) {
                break;
            }

            std::vector<std::size_t> alphaBetaGames;
            for (std::size_t index = 0; index < games.size(); ++index) {
                if (!finished(games[index], config.maxPlies) && !neuralTurn(games[index])) {
                    alphaBetaGames.push_back(index);
                }
            }
            std::vector<std::optional<Move>> alphaBetaMoves(alphaBetaGames.size());
            std::atomic_size_t next{0};
            const int workerCount = std::min<int>(config.threads, alphaBetaGames.size());
            std::vector<std::thread> workers;
            workers.reserve(workerCount);
            for (int worker = 0; worker < workerCount; ++worker) {
                workers.emplace_back([&] {
                    while (true) {
                        const std::size_t task = next.fetch_add(1);
                        if (task >= alphaBetaGames.size()) return;
                        alphaBetaMoves[task] = alphaBeta.chooseAction(
                            games[alphaBetaGames[task]].core);
                    }
                });
            }
            for (auto& worker : workers) worker.join();
            for (std::size_t task = 0; task < alphaBetaGames.size(); ++task) {
                if (!alphaBetaMoves[task] ||
                    !games[alphaBetaGames[task]].core.applyAction(*alphaBetaMoves[task])) {
                    throw std::runtime_error("Alpha-Beta returned an illegal action");
                }
                ++games[alphaBetaGames[task]].plies;
            }

            reportFinished(games, config.maxPlies);
            std::vector<std::size_t> neuralGames;
            for (std::size_t index = 0; index < games.size(); ++index) {
                if (!finished(games[index], config.maxPlies) && neuralTurn(games[index])) {
                    neuralGames.push_back(index);
                }
            }
            if (neuralGames.empty()) continue;

            std::cout << "B\t" << neuralGames.size() << '\n';
            for (const std::size_t index : neuralGames) {
                const auto actions = games[index].core.legalActions();
                std::cout << "P\t" << index << '\t'
                          << playerName(games[index].core.currentPlayer()) << '\t'
                          << games[index].core.serializeState() << '\t';
                for (std::size_t action = 0; action < actions.size(); ++action) {
                    if (action != 0) std::cout << ',';
                    std::cout << encodeAction(actions[action]);
                }
                std::cout << '\n';
            }
            std::cout.flush();

            for (const std::size_t expectedIndex : neuralGames) {
                std::string marker;
                std::size_t gameIndex = 0;
                int actionId = -1;
                if (!(std::cin >> marker >> gameIndex >> actionId) || marker != "A" ||
                    gameIndex != expectedIndex) {
                    throw std::runtime_error("invalid neural action response");
                }
                const Player player = games[gameIndex].core.currentPlayer();
                const Move move = decodeAction(actionId, player);
                if (!games[gameIndex].core.applyAction(move)) {
                    throw std::runtime_error("neural agent returned an illegal action");
                }
                ++games[gameIndex].plies;
            }
        }

        std::cout << "D\t" << config.games << '\n';
        std::cout.flush();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "mshogi_arena: " << error.what() << '\n';
        return 2;
    }
}
