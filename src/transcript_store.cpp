#include "agent/transcript_store.hpp"
#include "agent/event.hpp"
#include <array>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <filesystem>

namespace agent {

namespace fs = std::filesystem;

TranscriptStore::TranscriptStore(fs::path path) : path_(std::move(path)) {}

std::expected<void, TranscriptError> TranscriptStore::append(const JsonValue& event) {
    std::lock_guard lock(mutex_);
    // Ensure parent directory exists.
    std::error_code ec;
    fs::create_directories(path_.parent_path(), ec);
    if (ec)
        return std::unexpected(TranscriptError{"Cannot create transcript dir: " + ec.message()});

    // Recover an unterminated final record before appending, with fixed-size reads.
    if (fs::exists(path_, ec)) {
        auto end = fs::file_size(path_, ec);
        if (ec)
            return std::unexpected(TranscriptError{"Cannot stat transcript"});
        std::ifstream input(path_, std::ios::binary);
        if (!input)
            return std::unexpected(TranscriptError{"Cannot open transcript for recovery"});
        std::array<char, 4096> buffer{};
        auto position = end;
        bool found = false;
        while (position > 0 && !found) {
            const auto count = static_cast<size_t>(std::min<uintmax_t>(position, buffer.size()));
            position -= count;
            input.seekg(static_cast<std::streamoff>(position));
            input.read(buffer.data(), static_cast<std::streamsize>(count));
            if (!input)
                return std::unexpected(TranscriptError{"Cannot read transcript for recovery"});
            for (size_t i = count; i > 0; --i) {
                if (buffer[i - 1] == '\n') {
                    position += i;
                    found = true;
                    break;
                }
            }
        }
        input.close();
        if (position != end) {
            fs::resize_file(path_, position, ec);
            if (ec)
                return std::unexpected(TranscriptError{"Cannot recover transcript tail"});
        }
    }

    std::ofstream f(path_, std::ios::app | std::ios::binary);
    if (!f)
        return std::unexpected(TranscriptError{"Cannot open transcript: " + path_.string()});

    f << to_json_string(event) << '\n';
    f.flush();
    if (!f)
        return std::unexpected(TranscriptError{"Write failed: " + path_.string()});
    return {};
}

// Read the tail of the file within max_bytes budget, line by line.
std::expected<std::vector<JsonValue>, TranscriptError>
TranscriptStore::load_recent(size_t max_bytes) const {
    std::lock_guard lock(mutex_);
    std::error_code ec;
    if (!fs::exists(path_, ec))
        return std::vector<JsonValue>{};

    uintmax_t size = fs::file_size(path_, ec);
    if (ec)
        return std::unexpected(TranscriptError{"stat failed: " + ec.message()});

    std::ifstream f(path_, std::ios::binary);
    if (!f)
        return std::unexpected(TranscriptError{"Cannot open transcript: " + path_.string()});

    // Seek to start of budget window.
    uintmax_t skip = (size > static_cast<uintmax_t>(max_bytes)) ? size - max_bytes : 0;
    if (skip > 0) {
        // Do not drop a complete line when the window begins at a line boundary.
        f.seekg(static_cast<std::streamoff>(skip - 1));
        char preceding = 0;
        f.get(preceding);
        if (preceding != '\n') {
            // Skip partial line after seek.
            std::string discard;
            std::getline(f, discard);
        }
    }

    std::vector<JsonValue> events;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty())
            continue;
        auto parsed = parse_json(line);
        if (parsed)
            events.push_back(std::move(*parsed));
        // Tolerate unparseable lines (e.g., truncated last line).
    }
    return events;
}

std::expected<std::vector<JsonValue>, TranscriptError>
TranscriptStore::load_all(size_t max_bytes) const {
    std::lock_guard lock(mutex_);
    std::error_code ec;
    if (!fs::exists(path_, ec))
        return std::vector<JsonValue>{};
    uintmax_t size = fs::file_size(path_, ec);
    if (ec)
        return std::unexpected(TranscriptError{"Cannot stat transcript"});
    if (size > max_bytes)
        return std::unexpected(TranscriptError{"Full replay exceeds byte budget"});
    return load_recent(max_bytes);
}

std::expected<void, TranscriptError> TranscriptStore::compact(const std::string& summary,
                                                              size_t keep_recent_bytes) {
    std::lock_guard lock(mutex_);
    auto recent = load_recent(keep_recent_bytes);
    if (!recent)
        return std::unexpected(recent.error());
    // Count records with fixed buffers rather than materializing the complete log.
    std::ifstream input(path_, std::ios::binary);
    if (!input) {
        std::error_code ec;
        if (!fs::exists(path_, ec))
            return {};
        return std::unexpected(TranscriptError{"Cannot open transcript for compaction"});
    }
    uint64_t covered_before = 0;
    bool previous_summary = false;
    std::string first_line;
    char character = 0;
    while (first_line.size() < 65536 && input.get(character) && character != '\n')
        first_line.push_back(character);
    auto first = parse_json(first_line);
    if (first && first->is_object() && first->value("type", "") == "compaction_summary") {
        covered_before = first->value("covered_until_event", uint64_t{0});
        previous_summary = true;
    }
    input.clear();
    input.seekg(0);
    uint64_t total = 0;
    std::array<char, 4096> buffer{};
    while (input.read(buffer.data(), buffer.size()) || input.gcount() > 0)
        total += std::count(buffer.begin(), buffer.begin() + input.gcount(), '\n');
    if (input.bad())
        return std::unexpected(TranscriptError{"Cannot read transcript for compaction"});
    if (total == 0)
        return {};
    // Summary records are metadata, and must not reset the cumulative coverage counter.
    if (previous_summary && total > 0)
        --total;
    std::erase_if(*recent, [](const auto& event) {
        return event.is_object() && event.value("type", "") == "compaction_summary";
    });
    auto compaction = make_transcript_compaction(
        covered_before + (total > recent->size() ? total - recent->size() : 0), summary);

    // Write to temp file then atomically rename.
    auto tmp = path_;
    tmp += ".tmp";

    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f)
            return std::unexpected(TranscriptError{"Cannot open temp file: " + tmp.string()});

        f << to_json_string(compaction) << '\n';
        for (const auto& evt : *recent) {
            f << to_json_string(evt) << '\n';
        }
        f.flush();
        if (!f)
            return std::unexpected(TranscriptError{"Write failed during compaction"});
    }

    std::error_code ec;
    fs::rename(tmp, path_, ec);
    if (ec)
        return std::unexpected(TranscriptError{"Atomic rename failed: " + ec.message()});

    return {};
}

std::expected<uintmax_t, TranscriptError> TranscriptStore::byte_size() const {
    std::error_code ec;
    if (!fs::exists(path_, ec))
        return 0u;
    auto sz = fs::file_size(path_, ec);
    if (ec)
        return std::unexpected(TranscriptError{"stat failed: " + ec.message()});
    return sz;
}

} // namespace agent
