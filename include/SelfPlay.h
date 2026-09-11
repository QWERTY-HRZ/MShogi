#pragma once

#include <cstdint>
#include <string>
#include "GameAgent.h"

struct SelfPlayConfig {
    int games = 1;
    int searchDepth = 2;
    double temperature = 0.5;
    int temperaturePlies = 12;
    int maxPlies = 200;
    std::uint64_t seed = 1;
    std::string outputPath = "selfplay.jsonl.gz";
    std::string coreCommit = "unknown";
};

struct SelfPlaySummary {
    int games = 0;
    std::uint64_t positions = 0;
    int senteWins = 0;
    int goteWins = 0;
    int draws = 0;
    int truncatedGames = 0;
};

int encodeAction(const Move& move);
Move decodeAction(int actionId, Player player);

class SelfPlayRunner {
public:
    // 生成可重放的 JSONL，并使用 gzip 压缩为单个训练数据分片。
    static SelfPlaySummary run(const SelfPlayConfig& config);
};
