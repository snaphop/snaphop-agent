#include "agent/session_manager.hpp"
#include <filesystem>
#include <stdexcept>

namespace agent {

namespace fs = std::filesystem;

SessionManager::SessionManager(Config cfg) : cfg_(std::move(cfg)) {}

fs::path SessionManager::session_dir(const std::string& session_id) const {
    return fs::path(cfg_.runtime.data_dir) / "sessions" / session_id;
}

fs::path SessionManager::transcript_path(const std::string& session_id) const {
    return session_dir(session_id) / "transcript.jsonl";
}

std::expected<SessionState*, SessionError>
SessionManager::get_or_create(const std::string& session_id, const fs::path& workspace_override) {
    if (session_id.empty() || session_id.size() > 128 || session_id == "." || session_id == ".." ||
        session_id.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.") !=
            std::string::npos)
        return std::unexpected(
            SessionError{"Invalid session ID (use letters, digits, underscore, dash or dot)"});
    std::lock_guard<std::mutex> lock(sessions_mutex_);

    auto it = sessions_.find(session_id);
    if (it != sessions_.end())
        return it->second.get();

    if (sessions_.size() >= cfg_.limits.max_cached_sessions) {
        return std::unexpected(SessionError{"Max cached sessions (" +
                                            std::to_string(cfg_.limits.max_cached_sessions) +
                                            ") reached"});
    }

    fs::path ws =
        workspace_override.empty() ? fs::path(cfg_.runtime.default_workspace) : workspace_override;

    // Create workspace and session data dir.
    std::error_code ec;
    fs::create_directories(ws, ec);
    if (ec)
        return std::unexpected(SessionError{"Cannot create workspace: " + ec.message()});

    fs::create_directories(session_dir(session_id), ec);
    if (ec)
        return std::unexpected(SessionError{"Cannot create session dir: " + ec.message()});

    auto state = std::make_unique<SessionState>(session_id, ws, transcript_path(session_id));

    auto* ptr = state.get();
    sessions_.emplace(session_id, std::move(state));
    return ptr;
}

size_t SessionManager::active_count() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    return sessions_.size();
}

} // namespace agent
