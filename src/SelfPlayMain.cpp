#include "SelfPlay.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#ifndef MSHOGI_CORE_COMMIT
#define MSHOGI_CORE_COMMIT "unknown"
#endif

namespace {
void printUsage() {
    std::cout << "Usage: mshogi_selfplay [options]\n"
              << "  --games N               number of games (default 1)\n"
              << "  --output PATH           .jsonl.gz output path\n"
              << "  --seed N                deterministic seed\n"
              << "  --depth N               Alpha-Beta depth (default 2)\n"
              << "  --temperature X         opening softmax temperature (default 0.5)\n"
              << "  --temperature-plies N   plies using temperature (default 12)\n"
              << "  --max-plies N           truncate long games (default 200)\n"
              << "  --core-commit HASH      source commit stored in metadata\n";
}

std::string requireValue(int& index, int argc, char* argv[]) {
    if (++index >= argc) throw std::invalid_argument("missing option value");
    return argv[index];
}
}

int main(int argc, char* argv[]) {
    try {
        SelfPlayConfig config;
        config.coreCommit = MSHOGI_CORE_COMMIT;
        for (int i = 1; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--help") {
                printUsage();
                return 0;
            }
            if (option == "--games") config.games = std::stoi(requireValue(i, argc, argv));
            else if (option == "--output") config.outputPath = requireValue(i, argc, argv);
            else if (option == "--seed") config.seed = std::stoull(requireValue(i, argc, argv));
            else if (option == "--depth") config.searchDepth = std::stoi(requireValue(i, argc, argv));
            else if (option == "--temperature") config.temperature = std::stod(requireValue(i, argc, argv));
            else if (option == "--temperature-plies") config.temperaturePlies = std::stoi(requireValue(i, argc, argv));
            else if (option == "--max-plies") config.maxPlies = std::stoi(requireValue(i, argc, argv));
            else if (option == "--core-commit") config.coreCommit = requireValue(i, argc, argv);
            else throw std::invalid_argument("unknown option: " + option);
        }

        const SelfPlaySummary summary = SelfPlayRunner::run(config);
        std::cout << "games=" << summary.games
                  << " positions=" << summary.positions
                  << " sente_wins=" << summary.senteWins
                  << " gote_wins=" << summary.goteWins
                  << " draws=" << summary.draws
                  << " truncated=" << summary.truncatedGames
                  << " output=" << config.outputPath << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "mshogi_selfplay: " << error.what() << '\n';
        printUsage();
        return 2;
    }
}
