#pragma once

#include "json.hpp"
#include <filesystem>
#include <string>
#include <vector>
#include <expected>
#include <cstdint>
#include <mutex>

namespace agent {

struct TranscriptError {
    std::string message;
};

// Append-only JSONL transcript log for a session.
class TranscriptStore {
  public:
    // Initialize with the session transcript file path.
    explicit TranscriptStore(std::filesystem::path path);

    // Append one event. Atomic within a process (single writer per session).
    std::expected<void, TranscriptError> append(const JsonValue& event);

    // Load recent events within a byte budget. Reads from the end.
    std::expected<std::vector<JsonValue>, TranscriptError> load_recent(size_t max_bytes) const;

    // Diagnostic full replay. Fails rather than exceeding the supplied byte bound.
    std::expected<std::vector<JsonValue>, TranscriptError>
    load_all(size_t max_bytes = 4194304) const;

    // Compact: ask for a summary string, write compaction_summary + recent tail,
    // using temp file and atomic rename.
    std::expected<void, TranscriptError> compact(const std::string& summary,
                                                 size_t keep_recent_bytes = 16384);

    // Number of bytes in the transcript file.
    std::expected<uintmax_t, TranscriptError> byte_size() const;

    const std::filesystem::path& path() const {
        return path_;
    }

  private:
    std::filesystem::path path_;
    mutable std::recursive_mutex mutex_;
};

} // namespace agent
