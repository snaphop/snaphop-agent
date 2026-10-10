#include <catch2/catch_test_macros.hpp>
#include "agent/memory_manager.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>

namespace {
struct MemoryDirectory {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("agent_memory_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~MemoryDirectory() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};
} // namespace

TEST_CASE("Memory: summaries persist, replace atomically and load within a bound", "[memory]") {
    MemoryDirectory dir;
    agent::MemoryManager memory(dir.path);
    auto absent = memory.load_summary("s");
    REQUIRE(absent);
    CHECK_FALSE(absent->has_value());
    REQUIRE(memory.save_summary("s", "first goal"));
    agent::MemoryManager reopened(dir.path);
    auto summary = reopened.load_summary("s");
    REQUIRE(summary);
    REQUIRE(summary->has_value());
    CHECK(**summary == "first goal");
    REQUIRE(memory.save_summary("s", std::string(70000, 'x')));
    summary = reopened.load_summary("s");
    REQUIRE(summary);
    REQUIRE(summary->has_value());
    CHECK((**summary).size() == 65536);
    CHECK_FALSE(std::filesystem::exists(dir.path / "sessions/s/memory/summary.txt.tmp"));
}

TEST_CASE("Memory: notes retain unrelated keys and recover malformed stores", "[memory]") {
    MemoryDirectory dir;
    agent::MemoryManager memory(dir.path);
    auto absent = memory.get_note("s", "goal");
    REQUIRE(absent);
    CHECK_FALSE(absent->has_value());
    REQUIRE(memory.set_note("s", "goal", "ship"));
    REQUIRE(memory.set_note("s", "language", "C++"));
    REQUIRE(memory.set_note("s", "goal", "test"));
    auto goal = memory.get_note("s", "goal");
    auto language = memory.get_note("s", "language");
    REQUIRE(goal);
    REQUIRE(goal->has_value());
    CHECK(**goal == "test");
    REQUIRE(language);
    REQUIRE(language->has_value());
    CHECK(**language == "C++");
    absent = memory.get_note("s", "unknown");
    REQUIRE(absent);
    CHECK_FALSE(absent->has_value());
    for (const auto* corrupt : {"{", "[]"}) {
        std::ofstream(dir.path / "sessions/s/memory/notes.json") << corrupt;
        absent = memory.get_note("s", "goal");
        REQUIRE(absent);
        CHECK_FALSE(absent->has_value());
        REQUIRE(memory.set_note("s", "goal", "recovered"));
        goal = memory.get_note("s", "goal");
        REQUIRE(goal);
        REQUIRE(goal->has_value());
        CHECK(**goal == "recovered");
    }
}

TEST_CASE("Memory: filesystem failures return errors without replacing summaries", "[memory]") {
    MemoryDirectory dir;
    std::filesystem::create_directories(dir.path);
    std::ofstream(dir.path / "blocked") << "file";
    agent::MemoryManager blocked(dir.path / "blocked");
    CHECK_FALSE(blocked.save_summary("s", "goal"));
    CHECK_FALSE(blocked.set_note("s", "goal", "ship"));

    agent::MemoryManager memory(dir.path);
    auto base = dir.path / "sessions/s/memory";
    REQUIRE(memory.save_summary("s", "original"));
    std::filesystem::create_directory(base / "summary.txt.tmp");
    CHECK_FALSE(memory.save_summary("s", "replacement"));
    std::filesystem::remove(base / "summary.txt.tmp");
    auto summary = memory.load_summary("s");
    REQUIRE(summary);
    REQUIRE(summary->has_value());
    CHECK(**summary == "original");
    std::filesystem::remove(base / "summary.txt");
    std::filesystem::create_directory(base / "summary.txt");
    CHECK_FALSE(memory.save_summary("s", "replacement"));
    std::filesystem::create_directory(base / "notes.json");
    CHECK_FALSE(memory.set_note("s", "goal", "ship"));
}
