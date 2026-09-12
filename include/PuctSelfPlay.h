#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include "PuctSearch.h"
#include "SelfPlay.h"

struct PuctSelfPlayConfig {
    int games = 8;
    int maxPlies = 120;
    int temperaturePlies = 12;
    double temperature = 1.0;
    std::uint64_t seed = 1;
    std::filesystem::path outputPath = "puct_selfplay.jsonl.gz";
    std::filesystem::path runtimePath;
    std::filesystem::path modelPath;
    std::string modelSha256;
    std::string coreCommit = "unknown";
    PuctConfig search;
};

struct PuctSelfPlaySummary : SelfPlaySummary {
    PuctStatistics searchStatistics;
    double elapsedSeconds = 0.0;
};

class PuctSelfPlayRunner {
public:
    static PuctSelfPlaySummary run(const PuctSelfPlayConfig& config);
};
