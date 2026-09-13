#include "PuctSearch.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef MSHOGI_CORE_COMMIT
#define MSHOGI_CORE_COMMIT "unknown"
#endif

namespace {
struct Config {
    std::string runtimePath;
    std::string modelPath;
    std::vector<int> budgets{8, 16, 32, 64};
    int positions = 100;
    int warmup = 8;
    int leavesPerBatch = 4;
    double virtualLoss = 1.0;
    double exploration = 1.5;
    std::uint64_t seed = 20260913;
};

std::string requireValue(int& index, int argc, char* argv[]) {
    if (++index >= argc) throw std::invalid_argument("缺少选项值");
    return argv[index];
}

std::vector<int> parseBudgets(const std::string& value) {
    std::vector<int> budgets;
    std::size_t start = 0;
    while (start < value.size()) {
        const std::size_t separator = value.find(',', start);
        budgets.push_back(std::stoi(value.substr(start, separator - start)));
        if (separator == std::string::npos) break;
        start = separator + 1;
    }
    return budgets;
}

double percentile(const std::vector<double>& sorted, double probability) {
    const std::size_t index = static_cast<std::size_t>(
        std::ceil(probability * sorted.size())) - 1;
    return sorted[std::min(index, sorted.size() - 1)];
}

std::uint64_t seedFor(std::uint64_t seed, int budget, int index) {
    return seed ^ (static_cast<std::uint64_t>(budget) << 32) ^
           (static_cast<std::uint64_t>(index + 1) * 0x9E3779B97F4A7C15ULL);
}

void applyOne(PuctBatchSearch& search, GameCore& core, std::uint64_t seed) {
    const auto result = search.search({&core}, {seed}, {0.0});
    if (!result[0] || !core.applyAction(result[0]->selectedAction)) {
        throw std::runtime_error("PUCT 延迟基准得到非法着法");
    }
    search.advance(0, result[0]->selectedAction, core);
}
}

int main(int argc, char* argv[]) {
    try {
        Config config;
        for (int index = 1; index < argc; ++index) {
            const std::string option = argv[index];
            if (option == "--runtime") config.runtimePath = requireValue(index, argc, argv);
            else if (option == "--model") config.modelPath = requireValue(index, argc, argv);
            else if (option == "--budgets") config.budgets = parseBudgets(requireValue(index, argc, argv));
            else if (option == "--positions") config.positions = std::stoi(requireValue(index, argc, argv));
            else if (option == "--warmup") config.warmup = std::stoi(requireValue(index, argc, argv));
            else if (option == "--leaves-per-batch") config.leavesPerBatch = std::stoi(requireValue(index, argc, argv));
            else if (option == "--virtual-loss") config.virtualLoss = std::stod(requireValue(index, argc, argv));
            else if (option == "--c-puct") config.exploration = std::stod(requireValue(index, argc, argv));
            else if (option == "--seed") config.seed = std::stoull(requireValue(index, argc, argv));
            else throw std::invalid_argument("未知选项: " + option);
        }
        if (config.runtimePath.empty() || config.modelPath.empty() ||
            config.positions <= 0 || config.warmup < 0 || config.leavesPerBatch <= 0 ||
            config.virtualLoss < 0.0 || config.budgets.empty() ||
            std::any_of(config.budgets.begin(), config.budgets.end(),
                        [](int budget) { return budget < 2; })) {
            throw std::invalid_argument("无效的 PUCT 延迟基准配置");
        }

        const auto loadStarted = std::chrono::steady_clock::now();
        OnnxEvaluator evaluator(config.runtimePath, config.modelPath);
        const double modelLoadMilliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - loadStarted).count();
        GameCore inferenceWarmup;
        evaluator.evaluate({inferenceWarmup.fork()});
        std::cout << "M\t" << MSHOGI_CORE_COMMIT << '\t'
                  << GameConstants::RULE_VERSION << '\t'
                  << modelLoadMilliseconds << '\n';
        for (const int budget : config.budgets) {
            PuctConfig searchConfig;
            searchConfig.simulations = budget;
            searchConfig.leavesPerBatch = config.leavesPerBatch;
            searchConfig.virtualLoss = config.virtualLoss;
            searchConfig.exploration = config.exploration;
            searchConfig.dirichletEpsilon = 0.0;
            PuctBatchSearch search(evaluator, searchConfig, 1);
            GameCore core;
            const auto coldStarted = std::chrono::steady_clock::now();
            applyOne(search, core, seedFor(config.seed, budget, 0));
            const double coldTreeMilliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - coldStarted).count();
            for (int index = 0; index < config.warmup; ++index) {
                if (core.isTerminal()) core.reset();
                applyOne(search, core, seedFor(config.seed, budget, 1000000 + index));
            }
            const PuctStatistics before = search.statistics();

            std::vector<double> milliseconds;
            milliseconds.reserve(config.positions);
            int gamesStarted = 1;
            for (int index = 0; index < config.positions; ++index) {
                if (core.isTerminal()) {
                    core.reset();
                    ++gamesStarted;
                }
                const auto started = std::chrono::steady_clock::now();
                applyOne(search, core, seedFor(config.seed, budget, index));
                milliseconds.push_back(std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - started).count());
            }
            const PuctStatistics after = search.statistics();
            std::sort(milliseconds.begin(), milliseconds.end());
            const double mean = std::accumulate(milliseconds.begin(), milliseconds.end(), 0.0) /
                                milliseconds.size();
            // 输出每步端到端搜索延迟，不包含模型加载和预热。
            std::cout << "B\t" << budget << '\t' << config.positions << '\t'
                      << gamesStarted << '\t' << coldTreeMilliseconds << '\t'
                      << mean << '\t'
                      << percentile(milliseconds, 0.50) << '\t'
                      << percentile(milliseconds, 0.95) << '\t'
                      << milliseconds.back() << '\t'
                      << after.inferenceBatches - before.inferenceBatches << '\t'
                      << after.inferencePositions - before.inferencePositions << '\t'
                      << after.treeReuseHits - before.treeReuseHits << '\t'
                      << after.maxInferenceBatch << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "mshogi_puct_benchmark: " << error.what() << '\n';
        return 2;
    }
}
