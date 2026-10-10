#include <catch2/catch_test_macros.hpp>
#include "agent/tool_loop_guard.hpp"

TEST_CASE("Tool loop guard detects alternating repeats but allows changing outcomes", "[loop]") {
    agent::ToolLoopGuard guard;
    agent::ToolCall call{"c", "read_file", {{"path", "a"}}};
    agent::ToolResult result{"c", true, "same", ""};
    for (int i = 0; i < 8; ++i) {
        call.arguments["path"] = i % 2 == 0 ? "a" : "b";
        CHECK(guard.observe(call, result, true) != agent::ToolLoopGuard::Decision::Stop);
    }
    call.arguments["path"] = "a";
    CHECK(guard.observe(call, result, true) != agent::ToolLoopGuard::Decision::Stop);
    call.arguments["path"] = "b";
    CHECK(guard.observe(call, result, true) == agent::ToolLoopGuard::Decision::Stop);
    result.content = "changed";
    CHECK(guard.observe(call, result, true) == agent::ToolLoopGuard::Decision::Continue);
    for (int i = 0; i < 1000; ++i) {
        result.content = i;
        CHECK(guard.observe(call, result, true) == agent::ToolLoopGuard::Decision::Continue);
    }
}

TEST_CASE("Tool loop guard counts errors but never treats successful writes as no progress",
          "[loop]") {
    agent::ToolLoopGuard writes;
    agent::ToolCall call{"c", "write_file", {{"path", "a"}, {"content", "text"}}};
    agent::ToolResult result{"c", true, {{"bytes_written", 4}}, ""};
    for (int i = 0; i < 20; ++i)
        CHECK(writes.observe(call, result, false) == agent::ToolLoopGuard::Decision::Continue);
    result.ok = false;
    result.error = "denied";
    for (int i = 0; i < 4; ++i)
        CHECK(writes.observe(call, result, false) != agent::ToolLoopGuard::Decision::Stop);
    CHECK(writes.observe(call, result, false) == agent::ToolLoopGuard::Decision::Stop);
}

TEST_CASE("Tool loop guard does not stop repeated reads amid productive observations", "[loop]") {
    agent::ToolLoopGuard guard;
    agent::ToolCall repeated{"r", "read_file", {{"path", "status"}}};
    agent::ToolCall progressing{"p", "read_file", {{"path", "changes"}}};
    agent::ToolResult constant{"r", true, "same", ""};
    agent::ToolResult changed{"p", true, 0, ""};
    for (int i = 0; i < 20; ++i) {
        changed.content = i;
        CHECK(guard.observe(progressing, changed, true) ==
              agent::ToolLoopGuard::Decision::Continue);
        CHECK(guard.observe(repeated, constant, true) == agent::ToolLoopGuard::Decision::Continue);
    }
}
