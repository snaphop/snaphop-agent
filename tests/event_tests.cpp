#include <catch2/catch_test_macros.hpp>
#include "agent/event.hpp"

TEST_CASE("Events: runtime and transcript records round-trip with correlation fields", "[events]") {
    const auto records = {agent::make_lifecycle_event("s", "t", "start"),
                          agent::make_assistant_delta("hello\nworld"),
                          agent::make_assistant_final("done", "s", "t"),
                          agent::make_tool_start("echo", "c"),
                          agent::make_tool_end("echo", "c", true),
                          agent::make_tool_end("echo", "c", false, "blocked"),
                          agent::make_error_event("failure", "s", "t"),
                          agent::make_transcript_user("t", "hello"),
                          agent::make_transcript_assistant("t", "done"),
                          agent::make_transcript_tool_call("t", "c", "echo", {{"text", "hi"}}),
                          agent::make_transcript_tool_result("t", "c", true, "hi"),
                          agent::make_transcript_tool_result("t", "c", false, nullptr, "blocked"),
                          agent::make_transcript_system_note("memory"),
                          agent::make_transcript_compaction(12, "summary"),
                          agent::make_transcript_error("t", "failure")};
    for (const auto& record : records) {
        CHECK(record["type"].is_string());
        CHECK(record["ts"].get<std::string>().ends_with("Z"));
        const auto parsed = agent::parse_json(agent::to_json_string(record));
        REQUIRE(parsed);
        CHECK(*parsed == record);
    }
    CHECK(agent::make_tool_end("echo", "c", false, "blocked")["error"] == "blocked");
    CHECK_FALSE(agent::make_tool_end("echo", "c", true).contains("error"));
    CHECK(agent::make_error_event("failure", "s", "t")["turn_id"] == "t");
    CHECK(agent::make_transcript_system_note("memory")["text"] == "memory");
}
