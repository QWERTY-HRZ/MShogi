#include "PuctSelfPlay.h"

#include <iostream>
#include <stdexcept>
#include <string>

#ifndef MSHOGI_CORE_COMMIT
#define MSHOGI_CORE_COMMIT "unknown"
#endif

namespace {
std::string requireValue(int& index, int argc, char* argv[]) {
    if (++index >= argc) throw std::invalid_argument("缺少选项值");
    return argv[index];
}

void printUsage() {
    std::cout << "Usage: mshogi_puct_selfplay --runtime PATH --model PATH "
                 "--model-sha256 HASH [options]\n"
              << "  --games N --output PATH --seed N --simulations N\n"
              << "  --c-puct X --dirichlet-alpha X --dirichlet-epsilon X\n"
              << "  --temperature X --temperature-plies N --max-plies N\n";
}
}

int main(int argc, char* argv[]) {
    try {
        PuctSelfPlayConfig config;
        config.coreCommit = MSHOGI_CORE_COMMIT;
        for (int index = 1; index < argc; ++index) {
            const std::string option = argv[index];
            if (option == "--help") { printUsage(); return 0; }
            if (option == "--games") config.games = std::stoi(requireValue(index, argc, argv));
            else if (option == "--output") config.outputPath = requireValue(index, argc, argv);
            else if (option == "--runtime") config.runtimePath = requireValue(index, argc, argv);
            else if (option == "--model") config.modelPath = requireValue(index, argc, argv);
            else if (option == "--model-sha256") config.modelSha256 = requireValue(index, argc, argv);
            else if (option == "--seed") config.seed = std::stoull(requireValue(index, argc, argv));
            else if (option == "--simulations") config.search.simulations = std::stoi(requireValue(index, argc, argv));
            else if (option == "--c-puct") config.search.exploration = std::stod(requireValue(index, argc, argv));
            else if (option == "--dirichlet-alpha") config.search.dirichletAlpha = std::stod(requireValue(index, argc, argv));
            else if (option == "--dirichlet-epsilon") config.search.dirichletEpsilon = std::stod(requireValue(index, argc, argv));
            else if (option == "--temperature") config.temperature = std::stod(requireValue(index, argc, argv));
            else if (option == "--temperature-plies") config.temperaturePlies = std::stoi(requireValue(index, argc, argv));
            else if (option == "--max-plies") config.maxPlies = std::stoi(requireValue(index, argc, argv));
            else if (option == "--core-commit") config.coreCommit = requireValue(index, argc, argv);
            else throw std::invalid_argument("未知选项: " + option);
        }
        const PuctSelfPlaySummary summary = PuctSelfPlayRunner::run(config);
        std::cout << "games=" << summary.games
                  << " positions=" << summary.positions
                  << " sente_wins=" << summary.senteWins
                  << " gote_wins=" << summary.goteWins
                  << " draws=" << summary.draws
                  << " truncated=" << summary.truncatedGames
                  << " simulations=" << summary.searchStatistics.simulations
                  << " inference_batches=" << summary.searchStatistics.inferenceBatches
                  << " inference_positions=" << summary.searchStatistics.inferencePositions
                  << " tree_reuse_hits=" << summary.searchStatistics.treeReuseHits
                  << " seconds=" << summary.elapsedSeconds
                  << " output=" << config.outputPath.string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "mshogi_puct_selfplay: " << error.what() << '\n';
        printUsage();
        return 2;
    }
}
