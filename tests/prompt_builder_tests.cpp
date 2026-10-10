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
    CHECK(req.system_prompt.find("Echo text") != std::string::npos);
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
