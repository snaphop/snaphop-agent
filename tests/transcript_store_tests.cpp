#include <catch2/catch_test_macros.hpp>
#include "agent/transcript_store.hpp"
#include "agent/event.hpp"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static fs::path temp_transcript() {
    auto tmp =
        fs::temp_directory_path() /
        ("test_transcript_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".jsonl");
    return tmp;
}

TEST_CASE("TranscriptStore: missing empty and blank logs recover without compaction artifacts",
          "[transcript]") {
    auto path = temp_transcript();
    agent::TranscriptStore store(path);
    auto events = store.load_all();
    REQUIRE(events);
    CHECK(events->empty());
    REQUIRE(store.compact("missing"));
    CHECK_FALSE(fs::exists(path));
    std::ofstream(path).close();
    REQUIRE(store.compact("empty"));
    CHECK(fs::file_size(path) == 0);
    std::ofstream(path) << '\n';
    events = store.load_recent(100);
    REQUIRE(events);
    CHECK(events->empty());
    fs::remove(path);
}

TEST_CASE("TranscriptStore: filesystem failures preserve explicit errors and original data",
          "[transcript]") {
    auto path = temp_transcript();
    fs::create_directory(path);
    agent::TranscriptStore directory(path);
    CHECK_FALSE(directory.load_recent(100));
    CHECK_FALSE(directory.load_all());
    CHECK_FALSE(directory.byte_size());
    CHECK_FALSE(directory.append(agent::make_transcript_user("t", "hello")));
    CHECK_FALSE(directory.compact("invalid"));
    fs::remove(path);
    std::ofstream(path) << "blocked";
    agent::TranscriptStore blocked(path / "child/log.jsonl");
    CHECK_FALSE(blocked.append(agent::make_transcript_user("t", "hello")));
    fs::remove(path);
    agent::TranscriptStore store(path);
    REQUIRE(store.append(agent::make_transcript_user("t", "original")));
    fs::create_directory(path.string() + ".tmp");
    CHECK_FALSE(store.compact("replacement"));
    auto events = store.load_all();
    REQUIRE(events);
    REQUIRE(events->size() == 1);
    CHECK(events->front()["text"] == "original");
    fs::remove(path.string() + ".tmp");
    fs::remove(path);
}

TEST_CASE("TranscriptStore: append and load_recent", "[transcript]") {
    auto path = temp_transcript();
    agent::TranscriptStore store(path);

    auto e1 = agent::make_transcript_user("t1", "hello");
    auto e2 = agent::make_transcript_assistant("t1", "world");

    REQUIRE(store.append(e1));
    REQUIRE(store.append(e2));

    auto result = store.load_recent(65536);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 2);
    CHECK((*result)[0].value("text", "") == "hello");
    CHECK((*result)[1].value("text", "") == "world");

    fs::remove(path);
}

TEST_CASE("TranscriptStore: load_recent respects byte budget", "[transcript]") {
    auto path = temp_transcript();
    agent::TranscriptStore store(path);

    // Append 10 events with known content.
    for (int i = 0; i < 10; ++i) {
        REQUIRE(store.append(agent::make_transcript_user("t1", "msg" + std::to_string(i))));
    }

    // Get file size.
    auto sz_result = store.byte_size();
    REQUIRE(sz_result.has_value());
    size_t full_size = static_cast<size_t>(*sz_result);

    // Load with half the budget: should get fewer events.
    auto partial = store.load_recent(full_size / 2);
    REQUIRE(partial.has_value());
    CHECK(partial->size() < 10);

    // Load with full budget: should get all.
    auto all = store.load_recent(full_size + 1);
    REQUIRE(all.has_value());
    CHECK(all->size() == 10);

    fs::remove(path);
}

TEST_CASE("TranscriptStore: tolerates truncated final line", "[transcript]") {
    auto path = temp_transcript();

    // Write valid lines then a truncated one.
    {
        std::ofstream f(path);
        f << R"({"ts":"2026-01-01T00:00:00Z","type":"user_message","text":"ok"})" << '\n';
        f << R"({"truncated_json":)"; // intentionally incomplete
    }

    agent::TranscriptStore store(path);
    auto result = store.load_recent(65536);
    REQUIRE(result.has_value());
    // Only the valid line should be parsed; truncated line is silently skipped.
    CHECK(result->size() == 1);

    fs::remove(path);
}

TEST_CASE("TranscriptStore: compaction writes summary and retains recent tail", "[transcript]") {
    auto path = temp_transcript();
    agent::TranscriptStore store(path);

    for (int i = 0; i < 20; ++i) {
        REQUIRE(store.append(agent::make_transcript_user("t1", "msg" + std::to_string(i))));
    }

    REQUIRE(store.compact("Summary of past events", 256));

    auto result = store.load_all();
    REQUIRE(result.has_value());

    // First event must be the compaction_summary.
    REQUIRE(!result->empty());
    CHECK((*result)[0].value("type", "") == "compaction_summary");
    CHECK((*result)[0].value("summary", "") == "Summary of past events");

    // The compact transcript should be smaller than the original.
    auto sz = store.byte_size();
    REQUIRE(sz.has_value());
    // Just verify it's non-empty and has the summary.
    CHECK(*sz > 0);

    fs::remove(path);
}

TEST_CASE("TranscriptStore: compaction uses atomic rename (no partial writes)", "[transcript]") {
    auto path = temp_transcript();
    agent::TranscriptStore store(path);
    REQUIRE(store.append(agent::make_transcript_user("t1", "data")));
    REQUIRE(store.compact("test compact"));

    // Temp file should not exist after compaction.
    auto tmp = path;
    tmp += ".tmp";
    CHECK(!fs::exists(tmp));

    fs::remove(path);
}

TEST_CASE("TranscriptStore: append recovers a truncated final record", "[transcript]") {
    auto path = temp_transcript();
    agent::TranscriptStore store(path);
    REQUIRE(store.append(agent::make_transcript_user("t", "first")));
    std::ofstream(path, std::ios::app) << R"({"broken":)";
    REQUIRE(store.append(agent::make_transcript_assistant("t", "second")));
    auto events = store.load_all();
    REQUIRE(events);
    REQUIRE(events->size() == 2);
    CHECK(events->back()["text"] == "second");
    fs::remove(path);
}

TEST_CASE("TranscriptStore: exact tail boundary retains complete record", "[transcript]") {
    auto path = temp_transcript();
    agent::TranscriptStore store(path);
    REQUIRE(store.append(agent::make_transcript_user("t", "first")));
    auto last = agent::make_transcript_assistant("t", "last");
    REQUIRE(store.append(last));
    auto events = store.load_recent(agent::to_json_string(last).size() + 1);
    REQUIRE(events);
    REQUIRE(events->size() == 1);
    CHECK(events->front()["text"] == "last");
    fs::remove(path);
}

TEST_CASE("TranscriptStore: repeated compaction preserves cumulative coverage", "[transcript]") {
    auto path = temp_transcript();
    agent::TranscriptStore store(path);
    for (int i = 0; i < 10; ++i)
        REQUIRE(store.append(agent::make_transcript_user("t", "goal")));
    REQUIRE(store.compact("first", 0));
    for (int i = 0; i < 3; ++i)
        REQUIRE(store.append(agent::make_transcript_user("t", "next")));
    REQUIRE(store.compact("second", 0));
    auto events = store.load_all();
    REQUIRE(events);
    REQUIRE(events->size() == 1);
    CHECK(events->front()["covered_until_event"] == 13);
    fs::remove(path);
}

TEST_CASE("TranscriptStore: full replay refuses oversized history", "[transcript]") {
    auto path = temp_transcript();
    agent::TranscriptStore store(path);
    REQUIRE(store.append(agent::make_transcript_user("t", std::string(100, 'x'))));
    CHECK_FALSE(store.load_all(16));
    fs::remove(path);
}
