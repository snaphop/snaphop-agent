#include <catch2/catch_test_macros.hpp>
#include "agent/prompt_builder.hpp"
#include "agent/event.hpp"

static agent::Config default_cfg() {
    agent::Config cfg;
    cfg.limits.max_prompt_bytes = 4096;
    cfg.model.model = "test-model";
    return cfg;
}

TEST_CASE("PromptBuilder: builds request with system prompt and user message", "[prompt]") {
    auto cfg = default_cfg();
    agent::PromptBuilder builder(cfg);

    auto req = builder.build({}, "hello world", {});
    CHECK(!req.system_prompt.empty());
    REQUIRE(!req.messages.empty());
    CHECK(req.messages.back().role == "user");
    CHECK(req.messages.back().content == "hello world");
}

TEST_CASE("PromptBuilder: respects byte budget by trimming history", "[prompt]") {
    auto cfg = default_cfg();
    cfg.limits.max_prompt_bytes = 1024; // tight but can fit the base prompt
    agent::PromptBuilder builder(cfg);

    // Build a long history.
    std::vector<agent::JsonValue> history;
    for (int i = 0; i < 50; ++i) {
        history.push_back(agent::make_transcript_user("t1", "message " + std::to_string(i)));
        history.push_back(agent::make_transcript_assistant("t1", "reply " + std::to_string(i)));
    }

    auto req = builder.build(history, "new question", {});
    // With a tight budget, history should be trimmed.
    // The last message is always the current user message.
    CHECK(req.messages.back().content == "new question");
    // There should be fewer than 101 messages in total.
    CHECK(req.messages.size() < 101);
    CHECK(agent::to_json_string(agent::anthropic_request_body(req)).size() <=
          cfg.limits.max_prompt_bytes);
}

TEST_CASE("PromptBuilder: injects tool definitions into system prompt", "[prompt]") {
    auto cfg = default_cfg();
    agent::PromptBuilder builder(cfg);

    std::vector<agent::ToolDefinition> tools = {
        {.name = "echo", .description = "Echo text", .params = {}, .dangerous = false}};

    auto req = builder.build({}, "test", tools);
    CHECK(req.system_prompt.find("echo") != std::string::npos);
    CHECK(req.system_prompt.find("Echo text") == std::string::npos);
    REQUIRE(req.tools.size() == 1);
    CHECK(req.tools[0].description == "Echo text");
}

TEST_CASE("PromptBuilder: memory summary appears in system prompt", "[prompt]") {
    auto cfg = default_cfg();
    agent::PromptBuilder builder(cfg);

    agent::PromptBuilderOptions opts;
    opts.memory_summary = "The user is building an AI agent.";

    auto req = builder.build({}, "continue", {}, opts);
    CHECK(req.system_prompt.find("The user is building an AI agent.") != std::string::npos);
}

TEST_CASE("PromptBuilder: model name set from config", "[prompt]") {
    auto cfg = default_cfg();
    cfg.model.model = "my-special-model";
    agent::PromptBuilder builder(cfg);

    auto req = builder.build({}, "hi", {});
    CHECK(req.model == "my-special-model");
}

TEST_CASE("PromptBuilder: rejects current turn that cannot fit", "[prompt]") {
    auto cfg = default_cfg();
    cfg.limits.max_prompt_bytes = 200;
    agent::PromptBuilder builder(cfg);
    REQUIRE_THROWS_AS(builder.build({}, "hello", {}), std::length_error);
}

TEST_CASE("PromptBuilder: escaping counts toward the serialized byte budget", "[prompt]") {
    auto cfg = default_cfg();
    cfg.model.provider = "openai";
    cfg.limits.max_prompt_bytes = 1200;
    agent::PromptBuilder builder(cfg);
    std::vector<agent::JsonValue> history;
    for (int i = 0; i < 8; ++i) {
        history.push_back(agent::make_transcript_user("t", std::string(100, '"')));
        history.push_back(agent::make_transcript_assistant("t", std::string(100, '\\')));
    }
    auto req = builder.build(history, "latest", {});
    CHECK(agent::to_json_string(agent::openai_request_body(req)).size() <= 1200);
    CHECK(req.messages.back().content == "latest");
}

TEST_CASE("PromptBuilder: trims aggregate tool output within current turn and retains pairing",
          "[prompt]") {
    auto cfg = default_cfg();
    for (const auto* provider : {"openai", "anthropic"}) {
        cfg.model.provider = provider;
        agent::PromptBuilder builder(cfg);
        std::vector<agent::JsonValue> events{agent::make_transcript_user("t", "Inspect files")};
        for (int i = 0; i < 3; ++i) {
            const auto id = "c" + std::to_string(i);
            events.push_back(agent::make_transcript_assistant("t", ""));
            events.push_back(
                agent::make_transcript_tool_call("t", id, "read_file", {{"path", "a.txt"}}));
            events.push_back(agent::make_transcript_tool_result(
                "t", id, true, {{"content", std::string(2000, 'x')}, {"path", "a.txt"}}));
        }
        const auto original = events;
        const auto req = builder.build(events, {}, {});
        const auto body = cfg.model.provider == "openai" ? agent::openai_request_body(req)
                                                         : agent::anthropic_request_body(req);
        CHECK(agent::to_json_string(body).size() <= cfg.limits.max_prompt_bytes);
        REQUIRE(req.messages.size() == 7);
        for (size_t i = 1; i < req.messages.size(); i += 2) {
            REQUIRE(req.messages[i].tool_calls.size() == 1);
            CHECK(req.messages[i + 1].tool_call_id == req.messages[i].tool_calls[0].call_id);
        }
        CHECK(events == original);
        const auto latest = agent::parse_json(req.messages.back().content);
        REQUIRE(latest.has_value());
        CHECK((*latest)["content"]["content"] == std::string(2000, 'x'));
    }
}

TEST_CASE("Prompt replay retains completed writes and marks missing outcomes unknown", "[prompt]") {
    auto cfg = default_cfg();
    agent::PromptBuilder builder(cfg);
    std::vector<agent::JsonValue> events{
        agent::make_transcript_user("t", "Write files"),
        agent::make_transcript_assistant("t", ""),
        agent::make_transcript_tool_call("t", "a", "write_file",
                                         {{"path", "a"}, {"content", "done"}}),
        agent::make_transcript_tool_call("t", "b", "write_file",
                                         {{"path", "b"}, {"content", "unknown"}}),
        agent::make_transcript_tool_result("t", "a", true, {{"path", "a"}, {"bytes_written", 4}}),
        agent::make_transcript_user("next", "Continue")};
    auto req = builder.build(events, {}, {});
    REQUIRE(req.messages.size() == 5);
    REQUIRE(req.messages[1].tool_calls.size() == 2);
    CHECK(req.messages[2].tool_call_id == "a");
    CHECK_FALSE(req.messages[2].tool_error);
    CHECK(req.messages[3].tool_call_id == "b");
    CHECK(req.messages[3].tool_error);
    CHECK(req.messages[3].content.find("outcome unknown") != std::string::npos);
    CHECK(req.messages.back().content == "Continue");
    CHECK(agent::openai_request_body(req)["messages"][4]["tool_call_id"] == "b");
    CHECK(agent::anthropic_request_body(req)["messages"][2]["content"][1]["is_error"] == true);
}

TEST_CASE("Prompt replay moves displaced results drops duplicates and scopes repeated IDs",
          "[prompt]") {
    agent::PromptBuilder builder(default_cfg());
    std::vector<agent::JsonValue> events{
        agent::make_transcript_user("t", "Read"),
        agent::make_transcript_assistant("t", ""),
        agent::make_transcript_tool_call("t", "same", "read_file", {{"path", "a"}}),
        agent::make_transcript_user("next", "Continue"),
        agent::make_transcript_tool_result("t", "same", true, "first"),
        agent::make_transcript_tool_result("t", "same", true, "duplicate"),
        agent::make_transcript_assistant("next", ""),
        agent::make_transcript_tool_call("next", "same", "read_file", {{"path", "b"}}),
        agent::make_transcript_tool_result("next", "same", true, "second"),
        agent::make_transcript_tool_result("next", "orphan", true, "orphan")};
    const auto req = builder.build(events, {}, {});
    REQUIRE(req.messages.size() == 6);
    CHECK(req.messages[2].content.find("first") != std::string::npos);
    CHECK(req.messages[3].content == "Continue");
    CHECK(req.messages[5].content.find("second") != std::string::npos);
}
