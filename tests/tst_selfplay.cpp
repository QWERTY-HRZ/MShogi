#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <vector>
#include <zlib.h>
#include "SelfPlay.h"

namespace {
std::string readGzip(const std::filesystem::path& path) {
    gzFile file = gzopen(path.string().c_str(), "rb");
    if (!file) return {};
    std::string content;
    std::vector<char> buffer(4096);
    int bytesRead = 0;
    while ((bytesRead = gzread(file, buffer.data(), static_cast<unsigned int>(buffer.size()))) > 0) {
        content.append(buffer.data(), bytesRead);
    }
    gzclose(file);
    return content;
}
}

TEST(SelfPlayTest, ActionEncodingUsesTheDocumented990ActionSpace) {
    const Move pawnMove = Move::makeMove(2, 4, 2, 3, Player::Sente);
    EXPECT_EQ(encodeAction(pawnMove), 677);
    const Move decodedMove = decodeAction(677, Player::Sente);
    EXPECT_EQ(decodedMove.fromX, 2);
    EXPECT_EQ(decodedMove.fromY, 4);
    EXPECT_EQ(decodedMove.toX, 2);
    EXPECT_EQ(decodedMove.toY, 3);

    const Move rookDrop = Move::makeDrop(1, 4, PieceType::Rook, Player::Gote);
    EXPECT_EQ(encodeAction(rookDrop), 921);
    const Move decodedDrop = decodeAction(921, Player::Gote);
    EXPECT_TRUE(decodedDrop.isDrop);
    EXPECT_EQ(decodedDrop.dropType, PieceType::Rook);
    EXPECT_EQ(decodedDrop.toX, 1);
    EXPECT_EQ(decodedDrop.toY, 4);
    EXPECT_THROW(decodeAction(990, Player::Sente), std::out_of_range);
}

TEST(SelfPlayTest, WritesDeterministicCompressedTrainingRecords) {
    const auto directory = std::filesystem::temp_directory_path();
    const auto firstPath = directory / "mshogi_selfplay_first.jsonl.gz";
    const auto secondPath = directory / "mshogi_selfplay_second.jsonl.gz";
    std::filesystem::remove(firstPath);
    std::filesystem::remove(secondPath);

    SelfPlayConfig config;
    config.games = 2;
    config.searchDepth = 1;
    config.temperature = 0.8;
    config.temperaturePlies = 8;
    config.maxPlies = 20;
    config.seed = 20260910;
    config.coreCommit = "test-commit";
    config.outputPath = firstPath.string();
    const SelfPlaySummary firstSummary = SelfPlayRunner::run(config);

    config.outputPath = secondPath.string();
    const SelfPlaySummary secondSummary = SelfPlayRunner::run(config);
    const std::string firstContent = readGzip(firstPath);
    const std::string secondContent = readGzip(secondPath);

    EXPECT_EQ(firstSummary.games, 2);
    EXPECT_GT(firstSummary.positions, 0u);
    EXPECT_EQ(firstSummary.positions, secondSummary.positions);
    EXPECT_EQ(firstContent, secondContent);
    EXPECT_NE(firstContent.find("\"rule_version\":\"v1.10.0\""), std::string::npos);
    EXPECT_NE(firstContent.find("\"core_commit\":\"test-commit\""), std::string::npos);
    EXPECT_NE(firstContent.find("\"legal_actions\":"), std::string::npos);
    EXPECT_NE(firstContent.find("\"selected_action\":"), std::string::npos);
    EXPECT_NE(firstContent.find("\"outcome\":"), std::string::npos);
    EXPECT_NE(firstContent.find("\"record_type\":\"game_end\""), std::string::npos);

    std::filesystem::remove(firstPath);
    std::filesystem::remove(secondPath);
}
