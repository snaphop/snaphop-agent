#include "agent/memory_manager.hpp"
#include "agent/json.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>

namespace agent {

namespace fs = std::filesystem;

MemoryManager::MemoryManager(fs::path data_dir) : data_dir_(std::move(data_dir)) {}

fs::path MemoryManager::session_dir(const std::string& session_id) const {
    return data_dir_ / "sessions" / session_id / "memory";
}

std::expected<std::optional<std::string>, MemoryError>
MemoryManager::load_summary(const std::string& session_id) const {
    auto path = session_dir(session_id) / "summary.txt";
    std::error_code ec;
    if (!fs::exists(path, ec)) return std::optional<std::string>{};

    std::ifstream f(path);
    if (!f) return std::unexpected(MemoryError{"Cannot open summary: " + path.string()});
    std::ostringstream ss;
    ss << f.rdbuf();
    return std::optional<std::string>{ss.str()};
}

std::expected<void, MemoryError>
MemoryManager::save_summary(const std::string& session_id, std::string_view summary) {
    auto dir = session_dir(session_id);
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return std::unexpected(MemoryError{"Cannot create memory dir: " + ec.message()});

    std::ofstream f(dir / "summary.txt", std::ios::trunc);
    if (!f) return std::unexpected(MemoryError{"Cannot write summary"});
    f << summary;
    return {};
}

std::expected<void, MemoryError>
MemoryManager::set_note(const std::string& session_id, std::string_view key, std::string_view value) {
    auto dir = session_dir(session_id);
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return std::unexpected(MemoryError{"Cannot create memory dir: " + ec.message()});

    auto notes_path = dir / "notes.json";
    JsonValue notes = JsonValue::object();

    if (fs::exists(notes_path, ec)) {
        std::ifstream f(notes_path);
        if (f) {
            std::ostringstream ss; ss << f.rdbuf();
            auto parsed = parse_json(ss.str());
            if (parsed && parsed->is_object()) notes = std::move(*parsed);
        }
    }

    notes[std::string(key)] = std::string(value);

    std::ofstream f(notes_path, std::ios::trunc);
    if (!f) return std::unexpected(MemoryError{"Cannot write notes"});
    f << to_json_string(notes);
    return {};
}

std::expected<std::optional<std::string>, MemoryError>
MemoryManager::get_note(const std::string& session_id, std::string_view key) const {
    auto notes_path = session_dir(session_id) / "notes.json";
    std::error_code ec;
    if (!fs::exists(notes_path, ec)) return std::optional<std::string>{};

    std::ifstream f(notes_path);
    if (!f) return std::unexpected(MemoryError{"Cannot open notes"});
    std::ostringstream ss; ss << f.rdbuf();
    auto parsed = parse_json(ss.str());
    if (!parsed || !parsed->is_object()) return std::optional<std::string>{};

    auto k = std::string(key);
    if (!parsed->contains(k)) return std::optional<std::string>{};
    return std::optional<std::string>{(*parsed)[k].get<std::string>()};
}

} // namespace agent
