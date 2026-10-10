#include <catch2/catch_test_macros.hpp>
#include "agent/agent_runtime.hpp"
#include "agent/model_client.hpp"
#include "agent/event.hpp"
#include <filesystem>
#include <memory>
#include <queue>
#include <thread>
#include <atomic>
#include <fstream>
#include <condition_variable>
#include <future>
#include <algorithm>

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
                  size_t transcript_bytes = 4194304, size_t result_bytes = 65536) {
    agent::Config cfg;
    cfg.runtime.data_dir = data_dir;
    cfg.runtime.default_workspace = data_dir + "/workspace";
    cfg.limits.max_turn_seconds = turn_seconds;
    cfg.limits.max_transcript_bytes = transcript_bytes;
    cfg.limits.max_tool_result_bytes = result_bytes;
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

TEST_CASE("AgentRuntime: invalid inputs never reach the model", "[runtime]") {
    const auto data_dir = "/tmp/agent_invalid_turn_" + std::to_string(getpid());
    auto [runtime, mock] = make_test_runtime(data_dir);
    CHECK_FALSE(runtime->run_turn({"", "hello", ""}));
    CHECK_FALSE(runtime->run_turn({"s", "", ""}));
    CHECK_FALSE(runtime->run_turn({"../outside", "hello", ""}));
    CHECK(mock->requests.empty());
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

TEST_CASE("AgentRuntime: repeated unchanged tools warn then stop early", "[runtime]") {
    auto data_dir = "/tmp/agent_loop_limit_" + std::to_string(getpid());
    auto [runtime, mock] = make_test_runtime(data_dir);
    for (int i = 0; i < 16; ++i) {
        agent::ModelResponse response;
        response.tool_calls.push_back({"call_" + std::to_string(i), "echo", {{"text", "loop"}}});
        mock->responses.push(response);
    }
    auto result = runtime->run_turn({"loop", "loop", ""});
    REQUIRE_FALSE(result);
    CHECK(result.error().message.find("Tool loop stopped") != std::string::npos);
    CHECK(mock->requests.size() == 5);
    const auto warning = agent::parse_json(mock->requests[3].messages.back().content);
    REQUIRE(warning.has_value());
    CHECK((*warning)["error"].get<std::string>().find("loop warning") != std::string::npos);
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

TEST_CASE("AgentRuntime: large file page reaches model and transcript with continuation",
          "[runtime]") {
    const std::string data_dir = "/tmp/agent_test_readpage_" + std::to_string(getpid());
    fs::create_directories(data_dir + "/ws");
    std::string text;
    for (int i = 0; i < 200; ++i)
        text += "\\\"\n世界";
    std::ofstream(data_dir + "/ws/large.txt", std::ios::binary) << text;
    auto [runtime, mock] = make_test_runtime(data_dir, 300, 4194304, 256);
    agent::ModelResponse read;
    read.finish_reason = agent::FinishReason::ToolUse;
    read.tool_calls.push_back({"read1", "read_file", {{"path", "large.txt"}}});
    mock->responses.push(read);
    agent::ModelResponse final;
    final.text = "Read a page.";
    mock->responses.push(final);
    const auto result = runtime->run_turn({"pages", "Read large.txt", data_dir + "/ws"});
    REQUIRE(result.has_value());
    REQUIRE(mock->requests.size() == 2);
    const auto payload = agent::parse_json(mock->requests[1].messages.back().content);
    REQUIRE(payload.has_value());
    REQUIRE((*payload)["ok"] == true);
    const auto& page = (*payload)["content"];
    REQUIRE(page.is_object());
    const auto prefix = page["content"].get<std::string>();
    CHECK_FALSE(prefix.empty());
    CHECK(text.starts_with(prefix));
    CHECK(page["truncated"] == true);
    CHECK(page["next_offset"] == prefix.size());
    CHECK(agent::to_json_string(page).size() <= 256);
    auto session = runtime->sessions().get_or_create("pages");
    REQUIRE(session.has_value());
    const auto events = (*session)->transcript.load_recent(4096);
    REQUIRE(events.has_value());
    bool found = false;
    for (const auto& event : *events) {
        if (event.value("type", "") == "tool_result") {
            found = true;
            CHECK(event["content"] == page);
        }
    }
    CHECK(found);
    fs::remove_all(data_dir);
}

TEST_CASE("AgentRuntime: redaction expansion cannot invalidate a read continuation", "[runtime]") {
    const std::string data_dir = "/tmp/agent_test_readredact_" + std::to_string(getpid());
    fs::create_directories(data_dir + "/ws");
    std::ofstream(data_dir + "/ws/secret.txt") << std::string(1000, '#');
    auto [runtime, mock] = make_test_runtime(data_dir, 300, 4194304, 256);
    const char* old = std::getenv("AGENT_API_KEY");
    const std::optional<std::string> saved = old ? std::optional<std::string>(old) : std::nullopt;
    struct RestoreEnvironment {
        const std::optional<std::string>& saved;
        ~RestoreEnvironment() {
            if (saved)
                setenv("AGENT_API_KEY", saved->c_str(), 1);
            else
                unsetenv("AGENT_API_KEY");
        }
    } restore{saved};
    setenv("AGENT_API_KEY", "#", 1);
    agent::ModelResponse read;
    read.finish_reason = agent::FinishReason::ToolUse;
    read.tool_calls.push_back({"read1", "read_file", {{"path", "secret.txt"}}});
    mock->responses.push(read);
    agent::ModelResponse final;
    final.text = "Retry with smaller limit.";
    mock->responses.push(final);
    REQUIRE(runtime->run_turn({"redact", "Read secret.txt", data_dir + "/ws"}));
    REQUIRE(mock->requests.size() == 2);
    const auto payload = agent::parse_json(mock->requests[1].messages.back().content);
    REQUIRE(payload.has_value());
    CHECK((*payload)["ok"] == false);
    CHECK((*payload)["content"].is_null());
    CHECK((*payload)["error"].get<std::string>().find("smaller limit") != std::string::npos);
    auto session = runtime->sessions().get_or_create("redact");
    REQUIRE(session.has_value());
    const auto events = (*session)->transcript.load_recent(4096);
    REQUIRE(events.has_value());
    for (const auto& event : *events)
        CHECK(agent::to_json_string(event).find('#') == std::string::npos);
    fs::remove_all(data_dir);
}

TEST_CASE("AgentRuntime: duplicate call IDs reject a whole batch before writes", "[runtime]") {
    const auto dir = "/tmp/agent_duplicate_" + std::to_string(getpid());
    auto [runtime, mock] = make_test_runtime(dir);
    agent::ModelResponse batch;
    batch.tool_calls = {{"same", "write_file", {{"path", "a"}, {"content", "a"}}},
                        {"same", "write_file", {{"path", "b"}, {"content", "b"}}}};
    mock->responses.push(batch);
    CHECK_FALSE(runtime->run_turn({"duplicates", "Write", ""}));
    CHECK_FALSE(fs::exists(dir + "/workspace/a"));
    CHECK_FALSE(fs::exists(dir + "/workspace/b"));
    fs::remove_all(dir);
}

TEST_CASE("AgentRuntime: iteration cap and total call cap remain independent of loop detection",
          "[runtime]") {
    for (bool total_limit : {false, true}) {
        const auto dir = "/tmp/agent_caps_" + std::to_string(getpid());
        auto [runtime, mock] = make_test_runtime(dir);
        const int batch_size = total_limit ? 32 : 1;
        for (int i = 0; i < 16; ++i) {
            agent::ModelResponse response;
            for (int j = 0; j < batch_size; ++j) {
                auto value = std::to_string(i * batch_size + j);
                response.tool_calls.push_back({"id_" + value, "echo", {{"text", value}}});
            }
            mock->responses.push(response);
        }
        const auto result = runtime->run_turn({"caps", "Echo", ""});
        REQUIRE_FALSE(result);
        CHECK(result.error().message.find(total_limit ? "Total tool call limit"
                                                      : "iteration limit") != std::string::npos);
        fs::remove_all(dir);
    }
}

TEST_CASE("AgentRuntime: mixed write/read batches run sequentially and retain live results",
          "[runtime]") {
    const auto dir = "/tmp/agent_mixed_" + std::to_string(getpid());
    auto [runtime, mock] = make_test_runtime(dir);
    agent::ModelResponse batch;
    batch.tool_calls = {{"w", "write_file", {{"path", "a"}, {"content", "written"}}},
                        {"r", "read_file", {{"path", "a"}}}};
    mock->responses.push(batch);
    agent::ModelResponse final;
    final.text = "done";
    mock->responses.push(final);
    REQUIRE(runtime->run_turn({"mixed", "Write then read", ""}));
    REQUIRE(mock->requests.size() == 2);
    const auto read = agent::parse_json(mock->requests[1].messages.back().content);
    REQUIRE(read.has_value());
    CHECK((*read)["content"]["content"] == "written");
    CHECK(mock->requests[0].system_prompt == mock->requests[1].system_prompt);
    CHECK(mock->requests[0].tool_schemas == mock->requests[1].tool_schemas);
    fs::remove_all(dir);
}

TEST_CASE("AgentRuntime: read-only batches overlap with bounded workers and ordered results",
          "[runtime]") {
    struct Coordination {
        std::mutex mutex;
        std::condition_variable_any ready;
        int active = 0, peak = 0;
        bool release = false;
    } state;
    class ReadTool final : public agent::ITool {
      public:
        Coordination& state;
        explicit ReadTool(Coordination& value) : state(value) {}
        agent::ToolDefinition definition() const override {
            return {.name = "coordinated_read",
                    .description = "Read",
                    .params = {{.name = "index",
                                .type = "integer",
                                .required = true,
                                .description = "Index"}},
                    .read_only = true};
        }
        agent::ToolResult call(const agent::ToolCall& call,
                               const agent::ToolContext& ctx) override {
            std::unique_lock lock(state.mutex);
            ++state.active;
            state.peak = std::max(state.peak, state.active);
            state.ready.notify_all();
            state.ready.wait(lock, ctx.stop, [&] { return state.release; });
            --state.active;
            return {call.call_id, !ctx.stop.stop_requested(), call.arguments["index"], ""};
        }
    };
    agent::Config cfg;
    cfg.runtime.data_dir = "/tmp/agent_parallel_" + std::to_string(getpid());
    cfg.runtime.default_workspace = cfg.runtime.data_dir + "/workspace";
    cfg.tools.allow = {"coordinated_read"};
    cfg.limits.max_parallel_tools = 2;
    cfg.limits.max_turn_seconds = 3;
    auto model = std::make_unique<MockModelClient>();
    auto* mock = model.get();
    agent::ModelResponse batch;
    for (int i = 0; i < 6; ++i)
        batch.tool_calls.push_back({"c" + std::to_string(i), "coordinated_read", {{"index", i}}});
    mock->responses.push(batch);
    agent::ModelResponse final;
    final.text = "done";
    mock->responses.push(final);
    auto tools = std::make_unique<agent::ToolRegistry>();
    tools->register_tool(std::make_unique<ReadTool>(state));
    agent::AgentRuntime runtime(cfg, std::move(model), std::move(tools),
                                std::make_unique<agent::ToolPolicy>(cfg.tools), nullptr);
    std::stop_source cancel;
    bool cancellation = false;
    SECTION("completion") {}
    SECTION("cancellation settles active and queued calls") {
        cancellation = true;
    }
    auto turn = std::async(std::launch::async, [&] {
        return runtime.run_turn({"parallel", "Read", ""}, cancel.get_token());
    });
    bool overlapped;
    {
        std::unique_lock lock(state.mutex);
        overlapped =
            state.ready.wait_for(lock, std::chrono::seconds(2), [&] { return state.active == 2; });
        if (!cancellation)
            state.release = true;
    }
    if (cancellation)
        cancel.request_stop();
    state.ready.notify_all();
    const auto result = turn.get();
    CHECK(overlapped);
    if (cancellation) {
        REQUIRE_FALSE(result.has_value());
        CHECK(state.active == 0);
        auto session = runtime.sessions().get_or_create("parallel");
        REQUIRE(session.has_value());
        const auto events = (*session)->transcript.load_recent(8192);
        REQUIRE(events.has_value());
        CHECK(std::count_if(events->begin(), events->end(), [](const auto& event) {
                  return event.value("type", "") == "tool_result";
              }) == 6);
        fs::remove_all(cfg.runtime.data_dir);
        return;
    }
    REQUIRE(result.has_value());
    CHECK(state.peak == 2);
    REQUIRE(mock->requests.size() == 2);
    REQUIRE(mock->requests[1].messages.size() == 8);
    for (int i = 0; i < 6; ++i) {
        const auto& message = mock->requests[1].messages[static_cast<size_t>(i) + 2];
        CHECK(message.tool_call_id == "c" + std::to_string(i));
        const auto payload = agent::parse_json(message.content);
        REQUIRE(payload.has_value());
        CHECK((*payload)["content"] == i);
    }
    auto session = runtime.sessions().get_or_create("parallel");
    REQUIRE(session.has_value());
    auto events = (*session)->transcript.load_recent(8192);
    REQUIRE(events.has_value());
    int index = 0;
    for (const auto& event : *events) {
        if (event.value("type", "") == "tool_result")
            CHECK(event["provider_call_id"] == "c" + std::to_string(index++));
    }
    CHECK(index == 6);
    fs::remove_all(cfg.runtime.data_dir);
}

TEST_CASE("AgentRuntime: interruption preserves completed writes without retrying effects",
          "[runtime]") {
    std::stop_source cancellation;
    class WriteAndCancel final : public agent::ITool {
      public:
        std::stop_source& source;
        explicit WriteAndCancel(std::stop_source& stop) : source(stop) {}
        agent::ToolDefinition definition() const override {
            return {.name = "write_and_cancel", .description = "Write", .params = {}};
        }
        agent::ToolResult call(const agent::ToolCall& call,
                               const agent::ToolContext& context) override {
            std::ofstream(context.workspace / "written") << "committed";
            source.request_stop();
            return {call.call_id, true, {{"path", "written"}, {"bytes_written", 9}}, ""};
        }
    };
    agent::Config cfg;
    cfg.runtime.data_dir = "/tmp/agent_interrupted_" + std::to_string(getpid());
    cfg.runtime.default_workspace = cfg.runtime.data_dir + "/workspace";
    cfg.tools.allow.push_back("write_and_cancel");
    auto model = std::make_unique<MockModelClient>();
    auto* mock = model.get();
    agent::ModelResponse batch;
    batch.tool_calls = {{"w", "write_and_cancel", agent::JsonValue::object()},
                        {"skipped", "write_file", {{"path", "never"}, {"content", "no"}}}};
    mock->responses.push(batch);
    agent::ModelResponse final;
    final.text = "Written file exists.";
    mock->responses.push(final);
    auto tools = std::make_unique<agent::ToolRegistry>();
    tools->register_tool(std::make_unique<WriteAndCancel>(cancellation));
    tools->register_tool(agent::make_write_file_tool());
    agent::AgentRuntime runtime(cfg, std::move(model), std::move(tools),
                                std::make_unique<agent::ToolPolicy>(cfg.tools), nullptr);
    CHECK_FALSE(runtime.run_turn({"interrupted", "Write two", ""}, cancellation.get_token()));
    CHECK(fs::exists(cfg.runtime.default_workspace + "/written"));
    CHECK_FALSE(fs::exists(cfg.runtime.default_workspace + "/never"));
    REQUIRE(runtime.run_turn({"interrupted", "What happened?", ""}));
    REQUIRE(mock->requests.size() == 2);
    const auto& messages = mock->requests[1].messages;
    REQUIRE(messages.size() == 5);
    CHECK(messages[2].tool_call_id == "w");
    CHECK_FALSE(messages[2].tool_error);
    CHECK(messages[3].tool_call_id == "skipped");
    CHECK(messages[3].tool_error);
    CHECK_FALSE(fs::exists(cfg.runtime.default_workspace + "/never"));
    fs::remove_all(cfg.runtime.data_dir);
}

TEST_CASE("AgentRuntime: local call IDs stay unique when provider IDs repeat across turns",
          "[runtime]") {
    const auto dir = "/tmp/agent_call_identity_" + std::to_string(getpid());
    auto [runtime, mock] = make_test_runtime(dir);
    for (const auto* value : {"one", "two"}) {
        agent::ModelResponse call;
        call.tool_calls.push_back({"provider-reused", "echo", {{"text", value}}});
        mock->responses.push(call);
        agent::ModelResponse final;
        final.text = value;
        mock->responses.push(final);
        REQUIRE(runtime->run_turn({"identity", value, ""}));
    }
    auto session = runtime->sessions().get_or_create("identity");
    REQUIRE(session.has_value());
    const auto events = (*session)->transcript.load_recent(8192);
    REQUIRE(events.has_value());
    std::vector<std::string> ids;
    for (const auto& event : *events) {
        if (event.value("type", "") == "tool_call") {
            ids.push_back(event["call_id"].get<std::string>());
            CHECK(event["provider_call_id"] == "provider-reused");
        }
    }
    REQUIRE(ids.size() == 2);
    CHECK(ids[0] != ids[1]);
    REQUIRE(mock->requests.size() == 4);
    CHECK(mock->requests[3].messages.back().tool_call_id == "provider-reused");
    const auto payload = agent::parse_json(mock->requests[3].messages.back().content);
    REQUIRE(payload.has_value());
    CHECK((*payload)["content"] == "two");
    fs::remove_all(dir);
}
