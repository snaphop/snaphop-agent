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

TEST_CASE("Model clients: cancellation and missing credentials avoid transport", "[model]") {
    agent::ModelRequest req;
    req.model = "local";
    std::stop_source stop;
    stop.request_stop();
    for (auto factory : {agent::make_openai_client, agent::make_anthropic_client}) {
        auto client = factory();
        CHECK(client->complete(req, stop.get_token()).finish_reason ==
              agent::FinishReason::Cancelled);
        CHECK(client->complete(req, {}).finish_reason == agent::FinishReason::Error);
        req.api_key = "fake-key";
        req.endpoint = "unsupported-scheme://local";
        CHECK(client->complete(req, {}).finish_reason == agent::FinishReason::Error);
        req.api_key.clear();
    }
}

TEST_CASE("Provider adapters project identical canonical schemas with declared element types",
          "[model]") {
    agent::ModelRequest req;
    req.tools = {{.name = "typed",
                  .description = "Typed",
                  .params = {{.name = "numbers",
                              .type = "array",
                              .required = true,
                              .description = "Integers",
                              .item_type = "integer"},
                             {.name = "limit",
                              .type = "integer",
                              .required = false,
                              .description = "Positive",
                              .minimum = 1}}}};
    req.tool_schemas.push_back(agent::tool_input_schema(req.tools[0]));
    auto openai = agent::openai_request_body(req);
    auto anthropic = agent::anthropic_request_body(req);
    const auto& schema = openai["tools"][0]["function"]["parameters"];
    CHECK(schema == anthropic["tools"][0]["input_schema"]);
    CHECK(schema["properties"]["numbers"]["items"]["type"] == "integer");
    CHECK(schema["properties"]["limit"]["minimum"] == 1);
    CHECK(schema["additionalProperties"] == false);
}
