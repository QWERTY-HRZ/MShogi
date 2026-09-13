#include "PuctSearch.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>
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
    bool candidateSente = false;
    int pairId = 0;
    int plies = 0;
    bool reported = false;
};

struct Config {
    int games = 20;
    int simulations = 16;
    int leavesPerBatch = 1;
    int openingPlies = 6;
    int maxPlies = 100;
    int alphaBetaDepth = 0;
    int threads = 1;
    double exploration = 1.5;
    double virtualLoss = 1.0;
    std::uint64_t seed = 1;
    std::string runtimePath;
    std::string candidatePath;
    std::string championPath;
};

std::string requireValue(int& index, int argc, char* argv[]) {
    if (++index >= argc) throw std::invalid_argument("缺少选项值");
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

bool candidateTurn(const ArenaGame& game) {
    return (game.core.currentPlayer() == Player::Sente) == game.candidateSente;
}

bool finished(const ArenaGame& game, int maxPlies) {
    return game.core.isTerminal() || game.plies >= maxPlies;
}

void applyOpening(ArenaGame& first, ArenaGame& second, int openingPlies,
                  std::uint64_t seed, int pairId) {
    AlphaBetaAgent openingAgent(2);
    for (int ply = 0; ply < openingPlies && !first.core.isTerminal(); ++ply) {
        const auto scored = openingAgent.scoreActions(first.core);
        const auto move = AlphaBetaAgent::selectAction(
            scored, 1.0, mixedSeed(seed, pairId, ply));
        if (!move || !first.core.applyAction(*move) || !second.core.applyAction(*move)) {
            throw std::runtime_error("无法生成成对开局");
        }
        ++first.plies;
        ++second.plies;
    }
}

void reportFinished(std::vector<ArenaGame>& games, int maxPlies) {
    for (std::size_t index = 0; index < games.size(); ++index) {
        ArenaGame& game = games[index];
        if (game.reported || !finished(game, maxPlies)) continue;
        const bool truncated = !game.core.isTerminal();
        std::cout << "R\t" << index << '\t' << game.pairId << '\t'
                  << (game.candidateSente ? 'S' : 'G') << '\t'
                  << game.core.winner() << '\t'
                  << reasonName(game.core.endReason(), truncated) << '\t'
                  << game.plies << '\t' << (truncated ? 1 : 0) << '\n';
        game.reported = true;
    }
}
}

int main(int argc, char* argv[]) {
    try {
        Config config;
        for (int index = 1; index < argc; ++index) {
            const std::string option = argv[index];
            if (option == "--runtime") config.runtimePath = requireValue(index, argc, argv);
            else if (option == "--candidate") config.candidatePath = requireValue(index, argc, argv);
            else if (option == "--champion") config.championPath = requireValue(index, argc, argv);
            else if (option == "--games") config.games = std::stoi(requireValue(index, argc, argv));
            else if (option == "--simulations") config.simulations = std::stoi(requireValue(index, argc, argv));
            else if (option == "--leaves-per-batch") config.leavesPerBatch = std::stoi(requireValue(index, argc, argv));
            else if (option == "--virtual-loss") config.virtualLoss = std::stod(requireValue(index, argc, argv));
            else if (option == "--c-puct") config.exploration = std::stod(requireValue(index, argc, argv));
            else if (option == "--opening-plies") config.openingPlies = std::stoi(requireValue(index, argc, argv));
            else if (option == "--max-plies") config.maxPlies = std::stoi(requireValue(index, argc, argv));
            else if (option == "--alpha-beta-depth") config.alphaBetaDepth = std::stoi(requireValue(index, argc, argv));
            else if (option == "--threads") config.threads = std::stoi(requireValue(index, argc, argv));
            else if (option == "--seed") config.seed = std::stoull(requireValue(index, argc, argv));
            else throw std::invalid_argument("未知选项: " + option);
        }
        if (config.games <= 0 || config.games % 2 != 0 || config.simulations < 2 ||
            config.leavesPerBatch <= 0 || config.virtualLoss < 0.0 ||
            config.alphaBetaDepth < 0 || config.threads <= 0 ||
            config.openingPlies < 0 || config.maxPlies <= config.openingPlies ||
            config.runtimePath.empty() || config.candidatePath.empty() ||
            (config.alphaBetaDepth <= 0 && config.championPath.empty())) {
            throw std::invalid_argument("无效的 PUCT arena 配置");
        }

        OnnxEvaluator candidateEvaluator(config.runtimePath, config.candidatePath);
        std::unique_ptr<OnnxEvaluator> championEvaluator;
        if (config.alphaBetaDepth <= 0) {
            championEvaluator = std::make_unique<OnnxEvaluator>(
                config.runtimePath, config.championPath);
        }
        PuctConfig searchConfig;
        searchConfig.simulations = config.simulations;
        searchConfig.leavesPerBatch = config.leavesPerBatch;
        searchConfig.exploration = config.exploration;
        searchConfig.virtualLoss = config.virtualLoss;
        searchConfig.dirichletEpsilon = 0.0;
        PuctBatchSearch candidateSearch(candidateEvaluator, searchConfig, config.games);
        std::unique_ptr<PuctBatchSearch> championSearch;
        if (championEvaluator) {
            championSearch = std::make_unique<PuctBatchSearch>(
                *championEvaluator, searchConfig, config.games);
        }
        AlphaBetaAgent alphaBeta(std::max(1, config.alphaBetaDepth));

        std::vector<ArenaGame> games(config.games);
        std::cout << "M\t" << MSHOGI_CORE_COMMIT << '\t'
                  << GameConstants::RULE_VERSION << '\t' << config.simulations << '\t'
                  << config.leavesPerBatch << '\t' << config.virtualLoss << '\t'
                  << (config.alphaBetaDepth > 0 ? "alphabeta" : "puct") << '\n';
        for (int pairId = 0; pairId < config.games / 2; ++pairId) {
            ArenaGame& candidateSente = games[pairId * 2];
            ArenaGame& candidateGote = games[pairId * 2 + 1];
            candidateSente.candidateSente = true;
            candidateGote.candidateSente = false;
            candidateSente.pairId = candidateGote.pairId = pairId;
            applyOpening(candidateSente, candidateGote, config.openingPlies,
                         config.seed, pairId);
            std::cout << "O\t" << pairId << '\t'
                      << candidateSente.core.serializeState() << '\n';
        }

        while (true) {
            reportFinished(games, config.maxPlies);
            if (std::all_of(games.begin(), games.end(), [](const ArenaGame& game) {
                    return game.reported;
                })) break;

            std::vector<const GameCore*> candidatePositions(games.size(), nullptr);
            std::vector<const GameCore*> championPositions(games.size(), nullptr);
            std::vector<std::uint64_t> seeds(games.size(), 0);
            std::vector<double> temperatures(games.size(), 0.0);
            std::vector<std::optional<Move>> alphaBetaMoves(games.size());
            std::vector<std::size_t> alphaBetaGames;
            for (std::size_t index = 0; index < games.size(); ++index) {
                if (finished(games[index], config.maxPlies)) continue;
                seeds[index] = mixedSeed(config.seed, games[index].pairId,
                                         games[index].plies);
                if (candidateTurn(games[index])) {
                    candidatePositions[index] = &games[index].core;
                } else if (config.alphaBetaDepth > 0) {
                    alphaBetaGames.push_back(index);
                } else {
                    championPositions[index] = &games[index].core;
                }
            }
            std::atomic_size_t nextAlphaBeta{0};
            const int workerCount = std::min<int>(config.threads, alphaBetaGames.size());
            std::vector<std::thread> workers;
            workers.reserve(workerCount);
            for (int worker = 0; worker < workerCount; ++worker) {
                workers.emplace_back([&] {
                    while (true) {
                        const std::size_t task = nextAlphaBeta.fetch_add(1);
                        if (task >= alphaBetaGames.size()) return;
                        const std::size_t gameIndex = alphaBetaGames[task];
                        alphaBetaMoves[gameIndex] = alphaBeta.chooseAction(
                            games[gameIndex].core);
                    }
                });
            }
            for (auto& worker : workers) worker.join();
            const auto candidateResults = candidateSearch.search(
                candidatePositions, seeds, temperatures);
            std::vector<std::optional<PuctResult>> championResults(games.size());
            if (championSearch) {
                championResults = championSearch->search(
                    championPositions, seeds, temperatures);
            }
            for (std::size_t index = 0; index < games.size(); ++index) {
                if (finished(games[index], config.maxPlies)) continue;
                std::optional<Move> selected;
                if (candidateTurn(games[index])) {
                    if (candidateResults[index]) {
                        selected = candidateResults[index]->selectedAction;
                    }
                } else if (config.alphaBetaDepth > 0) {
                    selected = alphaBetaMoves[index];
                } else if (championResults[index]) {
                    selected = championResults[index]->selectedAction;
                }
                if (!selected || !games[index].core.applyAction(*selected)) {
                    throw std::runtime_error("PUCT arena 得到非法着法");
                }
                ++games[index].plies;
                // 神经搜索树跟随双方实战着法前进，保留下一回合可复用的分支。
                candidateSearch.advance(index, *selected, games[index].core);
                if (championSearch) {
                    championSearch->advance(index, *selected, games[index].core);
                }
            }
        }

        const auto& candidateStats = candidateSearch.statistics();
        std::cout << "S\tC\t" << candidateStats.simulations << '\t'
                  << candidateStats.inferenceBatches << '\t'
                  << candidateStats.inferencePositions << '\t'
                  << candidateStats.treeReuseHits << '\t'
                  << candidateStats.maxInferenceBatch << '\n';
        if (championSearch) {
            const auto& championStats = championSearch->statistics();
            std::cout << "S\tH\t" << championStats.simulations << '\t'
                      << championStats.inferenceBatches << '\t'
                      << championStats.inferencePositions << '\t'
                      << championStats.treeReuseHits << '\t'
                      << championStats.maxInferenceBatch << '\n';
        }
        std::cout << "D\t" << config.games << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "mshogi_puct_arena: " << error.what() << '\n';
        return 2;
    }
}
