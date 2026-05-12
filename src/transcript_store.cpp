#include "agent/transcript_store.hpp"
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <filesystem>

namespace agent {

namespace fs = std::filesystem;

TranscriptStore::TranscriptStore(fs::path path) : path_(std::move(path)) {}

std::expected<void, TranscriptError> TranscriptStore::append(const JsonValue& event) {
    // Ensure parent directory exists.
    std::error_code ec;
    fs::create_directories(path_.parent_path(), ec);
    if (ec) return std::unexpected(TranscriptError{"Cannot create transcript dir: " + ec.message()});

    std::ofstream f(path_, std::ios::app | std::ios::binary);
    if (!f) return std::unexpected(TranscriptError{"Cannot open transcript: " + path_.string()});

    f << to_json_string(event) << '\n';
    if (!f) return std::unexpected(TranscriptError{"Write failed: " + path_.string()});
    return {};
}

// Read the tail of the file within max_bytes budget, line by line.
std::expected<std::vector<JsonValue>, TranscriptError>
TranscriptStore::load_recent(size_t max_bytes) const {
    std::error_code ec;
    if (!fs::exists(path_, ec)) return std::vector<JsonValue>{};

    uintmax_t size = fs::file_size(path_, ec);
    if (ec) return std::unexpected(TranscriptError{"stat failed: " + ec.message()});

    std::ifstream f(path_, std::ios::binary);
    if (!f) return std::unexpected(TranscriptError{"Cannot open transcript: " + path_.string()});

    // Seek to start of budget window.
    uintmax_t skip = (size > static_cast<uintmax_t>(max_bytes)) ? size - max_bytes : 0;
    if (skip > 0) {
        f.seekg(static_cast<std::streamoff>(skip));
        // Skip partial line after seek.
        std::string discard;
        std::getline(f, discard);
    }

    std::vector<JsonValue> events;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        auto parsed = parse_json(line);
        if (parsed) events.push_back(std::move(*parsed));
        // Tolerate unparseable lines (e.g., truncated last line).
    }
    return events;
}

std::expected<std::vector<JsonValue>, TranscriptError>
TranscriptStore::load_all() const {
    std::error_code ec;
    if (!fs::exists(path_, ec)) return std::vector<JsonValue>{};
    uintmax_t size = fs::file_size(path_, ec);
    return load_recent(static_cast<size_t>(size + 1));
}

std::expected<void, TranscriptError>
TranscriptStore::compact(const std::string& summary, size_t keep_recent_bytes) {
    auto all = load_all();
    if (!all) return std::unexpected(all.error());

    // Count total events for covered_until_event marker.
    size_t total = all->size();
    if (total == 0) return {};

    // Build compaction summary record.
    JsonValue compaction;
    compaction["ts"]                   = ""; // will be filled by make_transcript_compaction
    compaction["type"]                 = "compaction_summary";
    compaction["summary"]              = summary;
    compaction["covered_until_event"]  = total;

    // Get the tail to keep.
    auto recent = load_recent(keep_recent_bytes);
    if (!recent) return std::unexpected(recent.error());

    // Write to temp file then atomically rename.
    auto tmp = path_;
    tmp += ".tmp";

    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return std::unexpected(TranscriptError{"Cannot open temp file: " + tmp.string()});

        f << to_json_string(compaction) << '\n';
        for (const auto& evt : *recent) {
            f << to_json_string(evt) << '\n';
        }
        if (!f) return std::unexpected(TranscriptError{"Write failed during compaction"});
    }

    std::error_code ec;
    fs::rename(tmp, path_, ec);
    if (ec) return std::unexpected(TranscriptError{"Atomic rename failed: " + ec.message()});

    return {};
}

std::expected<uintmax_t, TranscriptError> TranscriptStore::byte_size() const {
    std::error_code ec;
    if (!fs::exists(path_, ec)) return 0u;
    auto sz = fs::file_size(path_, ec);
    if (ec) return std::unexpected(TranscriptError{"stat failed: " + ec.message()});
    return sz;
}

} // namespace agent
