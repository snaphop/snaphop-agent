#pragma once

#include "config.hpp"
#include "transcript_store.hpp"
#include <filesystem>
#include <string>
#include <mutex>
#include <unordered_map>
#include <memory>
#include <expected>

namespace agent {

struct SessionError {
    std::string message;
};

// Per-session state owned by the manager.
struct SessionState {
    std::string            session_id;
    std::filesystem::path  workspace;
    TranscriptStore        transcript;
    std::mutex             turn_mutex;   // serializes turns within a session

    explicit SessionState(std::string id, std::filesystem::path ws, std::filesystem::path transcript_path)
        : session_id(std::move(id))
        , workspace(std::move(ws))
        , transcript(std::move(transcript_path)) {}

    // Non-copyable, non-movable (mutex)
    SessionState(const SessionState&) = delete;
    SessionState& operator=(const SessionState&) = delete;
};

// Manages session lifecycle: creation, lookup, and cleanup.
class SessionManager {
public:
    explicit SessionManager(Config cfg);

    // Retrieve or create a session.
    std::expected<SessionState*, SessionError>
    get_or_create(const std::string& session_id, const std::filesystem::path& workspace_override = {});

    // Return the active session count.
    size_t active_count() const;

    const Config& config() const { return cfg_; }

private:
    std::filesystem::path session_dir(const std::string& session_id) const;
    std::filesystem::path transcript_path(const std::string& session_id) const;

    Config cfg_;
    mutable std::mutex sessions_mutex_;
    std::unordered_map<std::string, std::unique_ptr<SessionState>> sessions_;
};

} // namespace agent
