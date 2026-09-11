#include "SelfPlay.h"
#include <filesystem>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <zlib.h>

namespace {
constexpr int BoardActionCount = GameConstants::ROWS * GameConstants::COLS *
                                 GameConstants::ROWS * GameConstants::COLS;
constexpr int SquareCount = GameConstants::ROWS * GameConstants::COLS;

struct PositionRecord {
    int ply;
    Player player;
    std::string state;
    std::vector<ScoredAction> scoredActions;
    Move selectedAction;
    double temperature;
    std::uint64_t selectionSeed;
};

class GzipJsonWriter {
public:
    explicit GzipJsonWriter(const std::string& path) {
        const std::filesystem::path outputPath(path);
        if (outputPath.has_parent_path()) {
            std::filesystem::create_directories(outputPath.parent_path());
        }
        m_file = gzopen(path.c_str(), "wb9");
        if (!m_file) throw std::runtime_error("cannot open gzip output: " + path);
    }

    ~GzipJsonWriter() {
        if (m_file) gzclose(m_file);
    }

    void writeLine(const std::string& line) {
        const std::string record = line + '\n';
        const int written = gzwrite(m_file, record.data(),
                                    static_cast<unsigned int>(record.size()));
        if (written != static_cast<int>(record.size())) {
            throw std::runtime_error("failed to write gzip output");
        }
    }

private:
    gzFile m_file = nullptr;
};

char playerCode(Player player) {
    return player == Player::Sente ? 'S' : 'G';
}

const char* endReasonName(CoreEndReason reason, bool truncated) {
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

std::uint64_t actionSeed(std::uint64_t seed, int gameId, int ply) {
    std::uint64_t value = seed ^ (static_cast<std::uint64_t>(gameId + 1) *
                                  0x9E3779B97F4A7C15ULL);
    value ^= static_cast<std::uint64_t>(ply + 1) * 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 30;
    value *= 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 27;
    value *= 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
}

std::string positionJson(int gameId, const PositionRecord& record, int winner) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    const int outcome = winner == 0 ? 0
                                    : (winner == (record.player == Player::Sente ? 1 : 2)
                                           ? 1 : -1);
    output << "{\"record_type\":\"position\",\"game_id\":" << gameId
           << ",\"ply\":" << record.ply
           << ",\"player\":\"" << playerCode(record.player)
           << "\",\"state\":\"" << record.state
           << "\",\"temperature\":" << record.temperature
           << ",\"selection_seed\":" << record.selectionSeed
           << ",\"legal_actions\":[";
    for (std::size_t i = 0; i < record.scoredActions.size(); ++i) {
        if (i != 0) output << ',';
        output << '[' << encodeAction(record.scoredActions[i].move) << ','
               << std::setprecision(10) << record.scoredActions[i].score << ']';
    }
    output << "],\"selected_action\":" << encodeAction(record.selectedAction)
           << ",\"outcome\":" << outcome << '}';
    return output.str();
}
}

int encodeAction(const Move& move) {
    const int toSquare = move.toY * GameConstants::COLS + move.toX;
    if (!move.isDrop) {
        const int fromSquare = move.fromY * GameConstants::COLS + move.fromX;
        return fromSquare * SquareCount + toSquare;
    }

    int dropType = 2;
    if (move.dropType == PieceType::Rook) dropType = 0;
    else if (move.dropType == PieceType::Bishop) dropType = 1;
    return BoardActionCount + dropType * SquareCount + toSquare;
}

Move decodeAction(int actionId, Player player) {
    if (actionId < 0 || actionId >= BoardActionCount + 3 * SquareCount) {
        throw std::out_of_range("action id is outside 0..989");
    }
    if (actionId < BoardActionCount) {
        const int fromSquare = actionId / SquareCount;
        const int toSquare = actionId % SquareCount;
        return Move::makeMove(fromSquare % GameConstants::COLS,
                              fromSquare / GameConstants::COLS,
                              toSquare % GameConstants::COLS,
                              toSquare / GameConstants::COLS, player);
    }

    const int dropAction = actionId - BoardActionCount;
    const int dropTypeIndex = dropAction / SquareCount;
    const int toSquare = dropAction % SquareCount;
    const PieceType type = dropTypeIndex == 0 ? PieceType::Rook
                           : dropTypeIndex == 1 ? PieceType::Bishop
                                                : PieceType::Pawn;
    return Move::makeDrop(toSquare % GameConstants::COLS,
                          toSquare / GameConstants::COLS, type, player);
}

SelfPlaySummary SelfPlayRunner::run(const SelfPlayConfig& config) {
    if (config.games <= 0 || config.searchDepth <= 0 || config.maxPlies <= 0 ||
        config.temperature < 0.0 || config.temperaturePlies < 0) {
        throw std::invalid_argument("invalid self-play configuration");
    }

    GzipJsonWriter writer(config.outputPath);
    std::ostringstream metadata;
    metadata.imbue(std::locale::classic());
    metadata << "{\"record_type\":\"metadata\","
             << "\"format\":\"mshogi-selfplay-jsonl-gzip\","
             << "\"format_version\":2,\"rule_version\":\""
             << GameConstants::RULE_VERSION << "\","
             << "\"action_count\":990,\"action_encoding\":\"from30+to/drop\","
             << "\"core_commit\":\"" << config.coreCommit << "\","
             << "\"seed\":" << config.seed << ",\"games\":" << config.games
             << ",\"search_depth\":" << config.searchDepth
             << ",\"temperature\":" << config.temperature
             << ",\"temperature_plies\":" << config.temperaturePlies
             << ",\"max_plies\":" << config.maxPlies << '}';
    writer.writeLine(metadata.str());

    AlphaBetaAgent agent(config.searchDepth);
    SelfPlaySummary summary;
    for (int gameId = 0; gameId < config.games; ++gameId) {
        GameCore core;
        std::vector<PositionRecord> records;
        bool truncated = false;

        for (int ply = 0; ply < config.maxPlies && !core.isTerminal(); ++ply) {
            auto scoredActions = agent.scoreActions(core);
            if (scoredActions.empty()) break;
            const double moveTemperature = ply < config.temperaturePlies
                                               ? config.temperature : 0.0;
            const std::uint64_t selectionSeed = actionSeed(config.seed, gameId, ply);
            const auto selected = AlphaBetaAgent::selectAction(
                scoredActions, moveTemperature, selectionSeed);
            if (!selected) break;
            records.push_back({ply, core.currentPlayer(), core.serializeState(),
                               std::move(scoredActions), *selected,
                               moveTemperature, selectionSeed});
            if (!core.applyAction(*selected)) {
                throw std::runtime_error("agent selected an illegal action");
            }
        }
        if (!core.isTerminal()) truncated = true;

        const int winner = truncated ? 0 : core.winner();
        for (const auto& record : records) {
            writer.writeLine(positionJson(gameId, record, winner));
        }
        std::ostringstream ending;
        ending << "{\"record_type\":\"game_end\",\"game_id\":" << gameId
               << ",\"plies\":" << records.size() << ",\"winner\":" << winner
               << ",\"end_reason\":\""
               << endReasonName(core.endReason(), truncated)
               << "\",\"truncated\":" << (truncated ? "true" : "false") << '}';
        writer.writeLine(ending.str());

        ++summary.games;
        summary.positions += records.size();
        if (winner == 1) ++summary.senteWins;
        else if (winner == 2) ++summary.goteWins;
        else if (!truncated && core.endReason() == CoreEndReason::RepetitionDraw) {
            ++summary.draws;
        }
        if (truncated) ++summary.truncatedGames;
    }
    return summary;
}
