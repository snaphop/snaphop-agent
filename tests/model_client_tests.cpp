#include <catch2/catch_test_macros.hpp>
#include "agent/model_client.hpp"

static agent::ModelRequest tool_request() {
    agent::ModelRequest req;
    req.model = "test-model";
    req.messages = {{"user", "echo twice", "", ""},
                    {"assistant",
                     "",
                     "",
                     "",
                     {{"a", "echo", {{"text", "one"}}}, {"b", "echo", {{"text", "two"}}}}},
                    {"tool", "one", "a", ""},
                    {"tool", "blocked", "b", "", {}, true}};
    return req;
}

TEST_CASE("OpenAI request: native tool calls and correlated results", "[model]") {
    auto body = agent::openai_request_body(tool_request());
    auto& messages = body["messages"];
    REQUIRE(messages.size() == 4);
    REQUIRE(messages[1]["tool_calls"].size() == 2);
    CHECK(messages[1]["tool_calls"][0]["function"]["arguments"] == R"({"text":"one"})");
    CHECK(messages[2]["role"] == "tool");
    CHECK(messages[2]["tool_call_id"] == "a");
    CHECK(messages[3]["tool_call_id"] == "b");
}

TEST_CASE("Anthropic request: parallel results share the next user message", "[model]") {
    auto body = agent::anthropic_request_body(tool_request());
    auto& messages = body["messages"];
    REQUIRE(messages.size() == 3);
    REQUIRE(messages[1]["content"].size() == 2);
    CHECK(messages[1]["content"][0]["type"] == "tool_use");
    CHECK(messages[1]["content"][0]["input"]["text"] == "one");
    REQUIRE(messages[2]["content"].size() == 2);
    CHECK(messages[2]["content"][0]["tool_use_id"] == "a");
    CHECK(messages[2]["content"][1]["is_error"] == true);
}

TEST_CASE("Model clients: missing model is an explicit error without network", "[model]") {
    agent::ModelRequest req;
    auto client = agent::make_openai_client();
    CHECK(client->complete(req, {}).finish_reason == agent::FinishReason::Error);
    auto anthropic = agent::make_anthropic_client();
    CHECK(anthropic->complete(req, {}).finish_reason == agent::FinishReason::Error);
}
