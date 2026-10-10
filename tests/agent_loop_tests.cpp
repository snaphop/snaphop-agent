#include <catch2/catch_test_macros.hpp>
#include "agent/agent_runtime.hpp"
#include "agent/model_client.hpp"
#include "agent/event.hpp"
#include <filesystem>
#include <memory>
#include <queue>
#include <thread>
#include <atomic>

namespace fs = std::filesystem;

// ---- Mock model client ----

// Each call to complete() pops the next scripted response.
class MockModelClient : public agent::IModelClient {
  public:
    std::queue<agent::ModelResponse> responses;
    std::vector<agent::ModelRequest> requests;

    agent::ModelResponse complete(const agent::ModelRequest& request, std::stop_token,
                                  agent::DeltaCallback) override {
        requests.push_back(request);
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
} // namespace agent

static std::pair<std::unique_ptr<agent::AgentRuntime>, MockModelClient*>
make_test_runtime(const std::string& data_dir, uint32_t turn_seconds = 300,
                  size_t transcript_bytes = 4194304) {
    agent::Config cfg;
    cfg.runtime.data_dir = data_dir;
    cfg.runtime.default_workspace = data_dir + "/workspace";
    cfg.limits.max_turn_seconds = turn_seconds;
    cfg.limits.max_transcript_bytes = transcript_bytes;
    cfg.tools.allow = {"echo", "list_dir", "read_file", "write_file"};
    cfg.tools.deny = {"shell"};

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

    return {std::make_unique<agent::AgentRuntime>(cfg, std::move(mock), std::move(tools),
                                                  std::move(policy), std::move(memory)),
            raw_mock};
}

TEST_CASE("AgentRuntime: simple no-tool turn", "[runtime]") {
    std::string data_dir = "/tmp/agent_test_notool_" + std::to_string(getpid());
    auto [runtime, mock] = make_test_runtime(data_dir);

    // Script a single final response.
    agent::ModelResponse resp;
    resp.text = "Hello! I'm ready to help.";
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
    tc.call_id = "call_1";
    tc.name = "echo";
    tc.arguments = agent::JsonValue{{"text", "ping"}};
    tool_resp.tool_calls.push_back(tc);
    mock->responses.push(tool_resp);

    // Second model response: final answer.
    agent::ModelResponse final_resp;
    final_resp.text = "The echo returned: ping";
    final_resp.finish_reason = agent::FinishReason::Stop;
    mock->responses.push(final_resp);

    agent::TurnInput input{
        .session_id = "s2", .message = "Echo ping", .workspace = data_dir + "/ws"};
    auto result = runtime->run_turn(input);

    REQUIRE(result.has_value());
    CHECK(result->had_tool_calls);
    CHECK(result->tool_call_count == 1);
    CHECK(result->final_text == "The echo returned: ping");
    REQUIRE(mock->requests.size() == 2);
    REQUIRE(mock->requests[0].messages.size() == 1);
    CHECK(mock->requests[0].messages[0].content == "Echo ping");
    REQUIRE(mock->requests[1].messages.size() == 3);
    CHECK(mock->requests[1].messages[1].tool_calls.size() == 1);
    CHECK(mock->requests[1].messages[2].role == "tool");
    CHECK(mock->requests[1].messages[2].tool_call_id == "call_1");

    fs::remove_all(data_dir);
}

TEST_CASE("AgentRuntime: denied tool is blocked", "[runtime]") {
    std::string data_dir = "/tmp/agent_test_deny_" + std::to_string(getpid());
    auto [runtime, mock] = make_test_runtime(data_dir);

    // Model requests the denied 'shell' tool.
    agent::ModelResponse tool_resp;
    tool_resp.finish_reason = agent::FinishReason::ToolUse;
    agent::ModelToolCall tc;
    tc.call_id = "call_shell";
    tc.name = "shell";
    tc.arguments = agent::JsonValue{{"command", "rm -rf /"}};
    tool_resp.tool_calls.push_back(tc);
    mock->responses.push(tool_resp);

    // Model continues with final answer after tool error.
    agent::ModelResponse final_resp;
    final_resp.text = "I cannot run shell commands.";
    final_resp.finish_reason = agent::FinishReason::Stop;
    mock->responses.push(final_resp);

    agent::TurnInput input{
        .session_id = "s3", .message = "Run shell command", .workspace = data_dir + "/ws"};
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
    resp.text = "Done.";
    resp.finish_reason = agent::FinishReason::Stop;
    mock->responses.push(resp);

    agent::TurnInput input{
        .session_id = "persist_sess", .message = "remember this", .workspace = data_dir + "/ws"};
    REQUIRE(runtime->run_turn(input).has_value());

    // The transcript JSONL should now exist.
    fs::path transcript = fs::path(data_dir) / "sessions" / "persist_sess" / "transcript.jsonl";
    CHECK(fs::exists(transcript));
    CHECK(fs::file_size(transcript) > 0);

    fs::remove_all(data_dir);
}

TEST_CASE("AgentRuntime: pre-cancellation returns an error", "[runtime]") {
    auto data_dir = "/tmp/agent_cancel_" + std::to_string(getpid());
    auto [runtime, mock] = make_test_runtime(data_dir);
    std::stop_source source;
    source.request_stop();
    CHECK_FALSE(runtime->run_turn({"cancel", "hello", ""}, source.get_token()));
    CHECK(mock->requests.empty());
    fs::remove_all(data_dir);
}

TEST_CASE("AgentRuntime: exhausted tool loop returns an error", "[runtime]") {
    auto data_dir = "/tmp/agent_loop_limit_" + std::to_string(getpid());
    auto [runtime, mock] = make_test_runtime(data_dir);
    for (int i = 0; i < 16; ++i) {
        agent::ModelResponse response;
        response.tool_calls.push_back({"call_" + std::to_string(i), "echo", {{"text", "loop"}}});
        mock->responses.push(response);
    }
    auto result = runtime->run_turn({"loop", "loop", ""});
    REQUIRE_FALSE(result);
    CHECK(result.error().message.find("iteration limit") != std::string::npos);
    fs::remove_all(data_dir);
}

TEST_CASE("AgentRuntime: configured credential is redacted from transcripts", "[runtime]") {
    auto data_dir = "/tmp/agent_redact_" + std::to_string(getpid());
    auto [runtime, mock] = make_test_runtime(data_dir);
    const char* old = std::getenv("AGENT_API_KEY");
    const std::optional<std::string> saved = old ? std::optional<std::string>(old) : std::nullopt;
    setenv("AGENT_API_KEY", "test-private-credential", 1);
    agent::ModelResponse response;
    response.text = "test-private-credential";
    mock->responses.push(response);
    auto result = runtime->run_turn({"secret", "test-private-credential", ""});
    if (saved)
        setenv("AGENT_API_KEY", saved->c_str(), 1);
    else
        unsetenv("AGENT_API_KEY");
    REQUIRE(result);
    CHECK(result->final_text == "[REDACTED]");
    auto session = runtime->sessions().get_or_create("secret");
    REQUIRE(session);
    auto events = (*session)->transcript.load_all();
    REQUIRE(events);
    for (const auto& event : *events)
        CHECK(agent::to_json_string(event).find("test-private-credential") == std::string::npos);
    fs::remove_all(data_dir);
}

TEST_CASE("AgentRuntime: deadline cancels while waiting for a session lock", "[runtime]") {
    auto data_dir = "/tmp/agent_deadline_" + std::to_string(getpid());
    auto [runtime, mock] = make_test_runtime(data_dir, 1);
    auto session = runtime->sessions().get_or_create("busy");
    REQUIRE(session);
    std::unique_lock lock((*session)->turn_mutex);
    const auto start = std::chrono::steady_clock::now();
    auto result = runtime->run_turn({"busy", "hello", ""});
    REQUIRE_FALSE(result);
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(3));
    CHECK(mock->requests.empty());
    lock.unlock();
    fs::remove_all(data_dir);
}

TEST_CASE("AgentRuntime: transcript threshold triggers deterministic compaction", "[runtime]") {
    auto data_dir = "/tmp/agent_compact_" + std::to_string(getpid());
    auto [runtime, mock] = make_test_runtime(data_dir, 300, 512);
    for (int i = 0; i < 8; ++i) {
        agent::ModelResponse response;
        response.text = std::string(100, 'x');
        mock->responses.push(response);
        REQUIRE(runtime->run_turn({"compact", "keep working on C++", ""}));
    }
    auto session = runtime->sessions().get_or_create("compact");
    REQUIRE(session);
    auto events = (*session)->transcript.load_all();
    REQUIRE(events);
    REQUIRE_FALSE(events->empty());
    CHECK(events->front()["type"] == "compaction_summary");
    CHECK(events->front()["summary"].get<std::string>().find("C++") != std::string::npos);
    fs::remove_all(data_dir);
}

class ConcurrentModel final : public agent::IModelClient {
  public:
    std::atomic<int> active{0};
    std::atomic<int> maximum{0};
    agent::ModelResponse complete(const agent::ModelRequest&, std::stop_token,
                                  agent::DeltaCallback) override {
        const int count = active.fetch_add(1) + 1;
        int previous = maximum.load();
        while (previous < count && !maximum.compare_exchange_weak(previous, count)) {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        --active;
        agent::ModelResponse response;
        response.text = "done";
        return response;
    }
};

TEST_CASE("AgentRuntime: concurrency limits running turns rather than total sessions",
          "[runtime]") {
    agent::Config cfg;
    cfg.runtime.data_dir = "/tmp/agent_concurrent_" + std::to_string(getpid());
    cfg.runtime.default_workspace = cfg.runtime.data_dir + "/workspace";
    cfg.limits.max_concurrent_sessions = 2;
    auto model = std::make_unique<ConcurrentModel>();
    auto* observed = model.get();
    agent::AgentRuntime runtime(cfg, std::move(model), std::make_unique<agent::ToolRegistry>(),
                                std::make_unique<agent::ToolPolicy>(cfg.tools), nullptr);
    std::atomic<int> successes{0};
    std::vector<std::jthread> workers;
    for (int i = 0; i < 8; ++i) {
        workers.emplace_back([&, i] {
            if (runtime.run_turn({"session_" + std::to_string(i), "hello", ""}))
                ++successes;
        });
    }
    workers.clear();
    CHECK(successes == 8);
    CHECK(observed->maximum <= 2);
    CHECK(runtime.sessions().active_count() == 8);
    fs::remove_all(cfg.runtime.data_dir);
}

TEST_CASE("AgentRuntime: transcript I/O failures are returned", "[runtime]") {
    auto data_dir = "/tmp/agent_append_failure_" + std::to_string(getpid());
    auto [runtime, mock] = make_test_runtime(data_dir);
    auto session = runtime->sessions().get_or_create("bad");
    REQUIRE(session);
    fs::create_directory((*session)->transcript.path());
    auto result = runtime->run_turn({"bad", "hello", ""});
    REQUIRE_FALSE(result);
    CHECK(result.error().message.find("transcript") != std::string::npos);
    CHECK(mock->requests.empty());
    fs::remove_all(data_dir);
}
