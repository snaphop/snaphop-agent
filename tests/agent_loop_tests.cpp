#include <catch2/catch_test_macros.hpp>
#include "agent/agent_runtime.hpp"
#include "agent/model_client.hpp"
#include "agent/event.hpp"
#include <filesystem>
#include <memory>
#include <queue>

namespace fs = std::filesystem;

// ---- Mock model client ----

// Each call to complete() pops the next scripted response.
class MockModelClient : public agent::IModelClient {
public:
    std::queue<agent::ModelResponse> responses;

    agent::ModelResponse complete(
        const agent::ModelRequest&,
        std::stop_token,
        agent::DeltaCallback) override
    {
        if (responses.empty()) {
            agent::ModelResponse err;
            err.finish_reason = agent::FinishReason::Error;
            err.error_message = "MockModelClient: no more scripted responses";
            return err;
        }
        auto r = std::move(responses.front());
        responses.pop();
        return r;
    }
};

namespace agent {
std::unique_ptr<ITool> make_echo_tool();
std::unique_ptr<ITool> make_list_dir_tool();
std::unique_ptr<ITool> make_read_file_tool();
std::unique_ptr<ITool> make_write_file_tool();
std::unique_ptr<ITool> make_shell_tool();
}

static std::pair<agent::AgentRuntime*, MockModelClient*>
make_test_runtime(const std::string& data_dir)
{
    agent::Config cfg;
    cfg.runtime.data_dir          = data_dir;
    cfg.runtime.default_workspace = data_dir + "/workspace";
    cfg.tools.allow = {"echo", "list_dir", "read_file", "write_file"};
    cfg.tools.deny  = {"shell"};

    auto mock = std::make_unique<MockModelClient>();
    auto* raw_mock = mock.get();

    auto tools = std::make_unique<agent::ToolRegistry>();
    tools->register_tool(agent::make_echo_tool());
    tools->register_tool(agent::make_list_dir_tool());
    tools->register_tool(agent::make_read_file_tool());
    tools->register_tool(agent::make_write_file_tool());
    tools->register_tool(agent::make_shell_tool());

    auto policy = std::make_unique<agent::ToolPolicy>(cfg.tools);
    auto memory = std::make_unique<agent::MemoryManager>(cfg.runtime.data_dir);

    static std::vector<std::unique_ptr<agent::AgentRuntime>> runtimes;
    runtimes.push_back(std::make_unique<agent::AgentRuntime>(
        cfg, std::move(mock), std::move(tools), std::move(policy), std::move(memory)));

    return {runtimes.back().get(), raw_mock};
}

TEST_CASE("AgentRuntime: simple no-tool turn", "[runtime]") {
    std::string data_dir = "/tmp/agent_test_notool_" + std::to_string(getpid());
    auto [runtime, mock] = make_test_runtime(data_dir);

    // Script a single final response.
    agent::ModelResponse resp;
    resp.text          = "Hello! I'm ready to help.";
    resp.finish_reason = agent::FinishReason::Stop;
    mock->responses.push(resp);

    agent::TurnInput input{.session_id = "s1", .message = "Hello", .workspace = data_dir + "/ws"};
    auto result = runtime->run_turn(input);

    REQUIRE(result.has_value());
    CHECK(result->final_text == "Hello! I'm ready to help.");
    CHECK(!result->had_tool_calls);
    CHECK(result->tool_call_count == 0);

    fs::remove_all(data_dir);
}

TEST_CASE("AgentRuntime: turn with one tool call", "[runtime]") {
    std::string data_dir = "/tmp/agent_test_tool_" + std::to_string(getpid());
    auto [runtime, mock] = make_test_runtime(data_dir);

    // First model response: requests echo tool.
    agent::ModelResponse tool_resp;
    tool_resp.finish_reason = agent::FinishReason::ToolUse;
    agent::ModelToolCall tc;
    tc.call_id   = "call_1";
    tc.name      = "echo";
    tc.arguments = agent::JsonValue{{"text", "ping"}};
    tool_resp.tool_calls.push_back(tc);
    mock->responses.push(tool_resp);

    // Second model response: final answer.
    agent::ModelResponse final_resp;
    final_resp.text          = "The echo returned: ping";
    final_resp.finish_reason = agent::FinishReason::Stop;
    mock->responses.push(final_resp);

    agent::TurnInput input{
        .session_id = "s2",
        .message    = "Echo ping",
        .workspace  = data_dir + "/ws"
    };
    auto result = runtime->run_turn(input);

    REQUIRE(result.has_value());
    CHECK(result->had_tool_calls);
    CHECK(result->tool_call_count == 1);
    CHECK(result->final_text == "The echo returned: ping");

    fs::remove_all(data_dir);
}

TEST_CASE("AgentRuntime: denied tool is blocked", "[runtime]") {
    std::string data_dir = "/tmp/agent_test_deny_" + std::to_string(getpid());
    auto [runtime, mock] = make_test_runtime(data_dir);

    // Model requests the denied 'shell' tool.
    agent::ModelResponse tool_resp;
    tool_resp.finish_reason = agent::FinishReason::ToolUse;
    agent::ModelToolCall tc;
    tc.call_id   = "call_shell";
    tc.name      = "shell";
    tc.arguments = agent::JsonValue{{"command", "rm -rf /"}};
    tool_resp.tool_calls.push_back(tc);
    mock->responses.push(tool_resp);

    // Model continues with final answer after tool error.
    agent::ModelResponse final_resp;
    final_resp.text          = "I cannot run shell commands.";
    final_resp.finish_reason = agent::FinishReason::Stop;
    mock->responses.push(final_resp);

    agent::TurnInput input{
        .session_id = "s3",
        .message    = "Run shell command",
        .workspace  = data_dir + "/ws"
    };
    auto result = runtime->run_turn(input);
    REQUIRE(result.has_value());
    // Tool was attempted but denied; the runtime still completes the turn.
    CHECK(result->tool_call_count == 1);

    fs::remove_all(data_dir);
}

TEST_CASE("AgentRuntime: transcript is persisted after turn", "[runtime]") {
    std::string data_dir = "/tmp/agent_test_persist_" + std::to_string(getpid());
    auto [runtime, mock] = make_test_runtime(data_dir);

    agent::ModelResponse resp;
    resp.text          = "Done.";
    resp.finish_reason = agent::FinishReason::Stop;
    mock->responses.push(resp);

    agent::TurnInput input{
        .session_id = "persist_sess",
        .message    = "remember this",
        .workspace  = data_dir + "/ws"
    };
    REQUIRE(runtime->run_turn(input).has_value());

    // The transcript JSONL should now exist.
    fs::path transcript = fs::path(data_dir) / "sessions" / "persist_sess" / "transcript.jsonl";
    CHECK(fs::exists(transcript));
    CHECK(fs::file_size(transcript) > 0);

    fs::remove_all(data_dir);
}
