#include "PuctSelfPlay.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>
#include <zlib.h>

namespace {
struct PositionRecord {
    int ply = 0;
    Player player = Player::Sente;
    std::string state;
    std::vector<ActionVisit> visits;
    Move selectedAction;
    double temperature = 0.0;
    std::uint64_t selectionSeed = 0;
    int rootVisits = 0;
    bool reusedTree = false;
};

struct GameState {
    GameCore core;
    std::vector<PositionRecord> records;
    bool truncated = false;
};

class GzipJsonWriter {
public:
    explicit GzipJsonWriter(const std::filesystem::path& path) {
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
        m_file = gzopen(path.string().c_str(), "wb9");
        if (!m_file) throw std::runtime_error("无法创建 PUCT gzip 分片");
    }
    ~GzipJsonWriter() { if (m_file) gzclose(m_file); }

    void writeLine(const std::string& line) {
        const std::string record = line + '\n';
        const int written = gzwrite(m_file, record.data(),
                                    static_cast<unsigned int>(record.size()));
        if (written != static_cast<int>(record.size())) {
            throw std::runtime_error("写入 PUCT gzip 分片失败");
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

std::uint64_t mixedSeed(std::uint64_t seed, int gameId, int ply) {
    std::uint64_t value = seed ^ (static_cast<std::uint64_t>(gameId + 1) *
                                  0x9E3779B97F4A7C15ULL);
    value ^= static_cast<std::uint64_t>(ply + 1) * 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 30;
    value *= 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 27;
    value *= 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
}

bool validSha256(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char item) {
        return std::isxdigit(static_cast<unsigned char>(item)) != 0;
    });
}

std::string positionJson(int gameId, const PositionRecord& record, int winner) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    const int outcome = winner == 0 ? 0 :
        (winner == (record.player == Player::Sente ? 1 : 2) ? 1 : -1);
    output << "{\"record_type\":\"position\",\"game_id\":" << gameId
           << ",\"ply\":" << record.ply
           << ",\"player\":\"" << playerCode(record.player)
           << "\",\"state\":\"" << record.state
           << "\",\"temperature\":" << record.temperature
           << ",\"selection_seed\":" << record.selectionSeed
           << ",\"root_visits\":" << record.rootVisits
           << ",\"tree_reused\":" << (record.reusedTree ? "true" : "false")
           << ",\"legal_actions\":[";
    for (std::size_t index = 0; index < record.visits.size(); ++index) {
        if (index != 0) output << ',';
        output << '[' << encodeAction(record.visits[index].move) << ','
               << record.visits[index].visits << ']';
    }
    output << "],\"selected_action\":" << encodeAction(record.selectedAction)
           << ",\"outcome\":" << outcome << '}';
    return output.str();
}
}

PuctSelfPlaySummary PuctSelfPlayRunner::run(const PuctSelfPlayConfig& config) {
    if (config.games <= 0 || config.maxPlies <= 0 || config.temperaturePlies < 0 ||
        config.temperature < 0.0 || !validSha256(config.modelSha256)) {
        throw std::invalid_argument("无效的 PUCT 自博弈配置");
    }
    const auto started = std::chrono::steady_clock::now();
    OnnxEvaluator evaluator(config.runtimePath, config.modelPath);
    PuctBatchSearch search(evaluator, config.search,
                           static_cast<std::size_t>(config.games));
    std::vector<GameState> games(static_cast<std::size_t>(config.games));

    for (int ply = 0; ply < config.maxPlies; ++ply) {
        std::vector<const GameCore*> positions(games.size(), nullptr);
        std::vector<std::uint64_t> seeds(games.size(), 0);
        std::vector<double> temperatures(games.size(), 0.0);
        bool anyActive = false;
        for (std::size_t gameId = 0; gameId < games.size(); ++gameId) {
            if (games[gameId].core.isTerminal()) continue;
            anyActive = true;
            positions[gameId] = &games[gameId].core;
            seeds[gameId] = mixedSeed(config.seed, static_cast<int>(gameId), ply);
            temperatures[gameId] = ply < config.temperaturePlies
                                       ? config.temperature : 0.0;
        }
        if (!anyActive) break;

        const auto results = search.search(positions, seeds, temperatures);
        for (std::size_t gameId = 0; gameId < games.size(); ++gameId) {
            if (!positions[gameId]) continue;
            if (!results[gameId]) throw std::runtime_error("PUCT 未返回选着");
            const auto& result = *results[gameId];
            games[gameId].records.push_back({
                ply, games[gameId].core.currentPlayer(),
                games[gameId].core.serializeState(), result.actionVisits,
                result.selectedAction, temperatures[gameId], seeds[gameId],
                result.rootVisits, result.reusedTree,
            });
            if (!games[gameId].core.applyAction(result.selectedAction)) {
                throw std::runtime_error("PUCT 选择了非法着法");
            }
            search.advance(gameId, result.selectedAction, games[gameId].core);
        }
    }
    for (GameState& game : games) game.truncated = !game.core.isTerminal();

    GzipJsonWriter writer(config.outputPath);
    std::ostringstream metadata;
    metadata.imbue(std::locale::classic());
    metadata << "{\"record_type\":\"metadata\"," 
             << "\"format\":\"mshogi-selfplay-jsonl-gzip\"," 
             << "\"format_version\":3,\"rule_version\":\""
             << GameConstants::RULE_VERSION << "\",\"action_count\":990,"
             << "\"action_encoding\":\"from30+to/drop\"," 
             << "\"policy_target\":\"puct_visit_counts\"," 
             << "\"replay_buffer_version\":1,\"core_commit\":\""
             << config.coreCommit << "\",\"model_sha256\":\""
             << config.modelSha256 << "\",\"seed\":" << config.seed
             << ",\"games\":" << config.games
             << ",\"simulations\":" << config.search.simulations
             << ",\"c_puct\":" << config.search.exploration
             << ",\"dirichlet_alpha\":" << config.search.dirichletAlpha
             << ",\"dirichlet_epsilon\":" << config.search.dirichletEpsilon
             << ",\"temperature\":" << config.temperature
             << ",\"temperature_plies\":" << config.temperaturePlies
             << ",\"max_plies\":" << config.maxPlies << '}';
    writer.writeLine(metadata.str());

    PuctSelfPlaySummary summary;
    for (std::size_t gameId = 0; gameId < games.size(); ++gameId) {
        const int winner = games[gameId].truncated ? 0 : games[gameId].core.winner();
        for (const PositionRecord& record : games[gameId].records) {
            writer.writeLine(positionJson(static_cast<int>(gameId), record, winner));
        }
        std::ostringstream ending;
        ending << "{\"record_type\":\"game_end\",\"game_id\":" << gameId
               << ",\"plies\":" << games[gameId].records.size()
               << ",\"winner\":" << winner << ",\"end_reason\":\""
               << endReasonName(games[gameId].core.endReason(), games[gameId].truncated)
               << "\",\"truncated\":"
               << (games[gameId].truncated ? "true" : "false") << '}';
        writer.writeLine(ending.str());
        ++summary.games;
        summary.positions += games[gameId].records.size();
        if (winner == 1) ++summary.senteWins;
        else if (winner == 2) ++summary.goteWins;
        else if (!games[gameId].truncated) ++summary.draws;
        if (games[gameId].truncated) ++summary.truncatedGames;
    }
    summary.searchStatistics = search.statistics();
    summary.elapsedSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    return summary;
}
