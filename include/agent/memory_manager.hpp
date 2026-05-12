#pragma once

#include <filesystem>
#include <string>
#include <optional>
#include <expected>

namespace agent {

struct MemoryError {
    std::string message;
};

// File-backed memory: summary file and optional key-value notes.
// v0: no vector memory, no external DB.
class MemoryManager {
public:
    explicit MemoryManager(std::filesystem::path data_dir);

    // Load the session summary file, if present.
    std::expected<std::optional<std::string>, MemoryError>
    load_summary(const std::string& session_id) const;

    // Write a new summary for the session (overwrites).
    std::expected<void, MemoryError>
    save_summary(const std::string& session_id, std::string_view summary);

    // Set a key-value note for the session.
    std::expected<void, MemoryError>
    set_note(const std::string& session_id, std::string_view key, std::string_view value);

    // Get a key-value note.
    std::expected<std::optional<std::string>, MemoryError>
    get_note(const std::string& session_id, std::string_view key) const;

private:
    std::filesystem::path session_dir(const std::string& session_id) const;
    std::filesystem::path data_dir_;
};

} // namespace agent
