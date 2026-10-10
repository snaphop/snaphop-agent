#include "agent/agent_runtime.hpp"
#include "agent/event.hpp"
#include "agent/tool_result_budget.hpp"
#include <atomic>
#include <sstream>
#include <iomanip>
#include <random>
#include <chrono>
#include <thread>
#include <mutex>
#include <iostream>
#include <condition_variable>
#include <memory_resource>
#include <array>
#include <unordered_set>
#include <algorithm>

namespace agent {

// External factory declarations for tools.
std::unique_ptr<ITool> make_echo_tool();
std::unique_ptr<ITool> make_list_dir_tool();
std::unique_ptr<ITool> make_read_file_tool();
std::unique_ptr<ITool> make_write_file_tool();
std::unique_ptr<ITool> make_shell_tool();

AgentRuntime::AgentRuntime(Config cfg, std::unique_ptr<IModelClient> model_client,
                           std::unique_ptr<ToolRegistry> tools, std::unique_ptr<ToolPolicy> policy,
                           std::unique_ptr<MemoryManager> memory)
    : cfg_(std::move(cfg)), sessions_(cfg_), prompt_builder_(cfg_), memory_(std::move(memory)),
      model_client_(std::move(model_client)), tools_(std::move(tools)), policy_(std::move(policy)),
      tool_executor_(cfg_.limits.max_parallel_tools) {
    tools_->freeze();
    for (auto& definition : tools_->definitions()) {
        if (policy_->is_allowed(definition))
            allowed_tools_.push_back(std::move(definition));
    }
    std::sort(allowed_tools_.begin(), allowed_tools_.end(),
              [](const auto& a, const auto& b) { return a.name < b.name; });
    for (const auto& definition : allowed_tools_)
        allowed_tool_schemas_.push_back(tool_input_schema(definition));
    if (!memory_)
        memory_ = std::make_unique<MemoryManager>(cfg_.runtime.data_dir);
}

// Redact the configured credential wherever untrusted text reaches events or disk.
static std::string redact(std::string text, const Config& cfg) {
    const char* value = std::getenv(cfg.model.api_key_env.c_str());
    if (!value || !*value)
        return text;
    const std::string_view secret(value);
    size_t position = 0;
    while ((position = text.find(secret, position)) != std::string::npos) {
        text.replace(position, secret.size(), "[REDACTED]");
        position += 10;
    }
    return text;
}
static void redact_json(JsonValue& value, const Config& cfg) {
    if (value.is_string())
        value = redact(value.get<std::string>(), cfg);
    else if (value.is_object()) {
        auto safe = JsonValue::object();
        for (auto& [key, item] : value.items()) {
            redact_json(item, cfg);
            safe[redact(key, cfg)] = std::move(item);
        }
        value = std::move(safe);
    } else if (value.is_array()) {
        for (auto& item : value)
            redact_json(item, cfg);
    }
}

void AgentRuntime::emit(const JsonValue& event) const {
    auto safe = event;
    redact_json(safe, cfg_);
    emit_event(safe);
}

static std::string make_random_id(std::string_view prefix) {
    static std::atomic<uint64_t> counter{0};
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    auto n = counter.fetch_add(1, std::memory_order_relaxed);
    std::ostringstream ss;
    ss << prefix << "_" << std::hex << now << "_" << n;
    return ss.str();
}

std::string AgentRuntime::generate_turn_id() const {
    return make_random_id("t");
}

std::string AgentRuntime::generate_call_id() const {
    return make_random_id("c");
}

std::expected<bool, RuntimeError>
AgentRuntime::model_iteration(SessionState& session, const std::string& turn_id,
                              std::pmr::vector<JsonValue>& transcript_scratch,
                              std::string& final_text_out, int& tool_call_count, ModelRequest& req,
                              const PromptBuilderOptions& opts, ToolLoopGuard& loop_guard,
                              std::stop_token stop) {
    if (tool_call_count > 0)
        prompt_builder_.fit(req, opts);

    // Call the model.
    auto delta_cb = [&](std::string_view chunk) -> bool {
        emit(make_assistant_delta(redact(std::string(chunk), cfg_)));
        return !stop.stop_requested();
    };

    auto resp = model_client_->complete(req, stop, delta_cb);

    if (stop.stop_requested() || resp.finish_reason == FinishReason::Cancelled) {
        return std::unexpected(RuntimeError{"Turn cancelled", true});
    }

    if (resp.finish_reason == FinishReason::Error) {
        return std::unexpected(RuntimeError{redact(std::move(resp.error_message), cfg_), true});
    }

    resp.text = redact(std::move(resp.text), cfg_);
    // Handle tool calls.
    if (!resp.tool_calls.empty()) {
        if (resp.tool_calls.size() > 32)
            return std::unexpected(RuntimeError{"Too many tool calls in one model response"});
        if (static_cast<size_t>(tool_call_count) + resp.tool_calls.size() >
            ToolLoopGuard::max_calls)
            return std::unexpected(RuntimeError{"Total tool call limit exceeded"});
        // Reject duplicate provider IDs atomically before any call in the batch executes.
        std::unordered_set<std::string> ids;
        for (const auto& call : resp.tool_calls) {
            if (!call.call_id.empty() && !ids.insert(call.call_id).second)
                return std::unexpected(RuntimeError{"Duplicate tool call ID in model response"});
        }
        std::vector<ToolCall> calls;
        calls.reserve(resp.tool_calls.size());
        for (auto& call : resp.tool_calls) {
            if (call.call_id.empty()) {
                do {
                    call.call_id = generate_call_id();
                } while (!ids.insert(call.call_id).second);
            }
            calls.push_back({generate_call_id(), call.name, std::move(call.arguments)});
        }
        auto asst_evt = make_transcript_assistant(turn_id, resp.text);
        if (!session.transcript.append(asst_evt))
            return std::unexpected(RuntimeError{"Transcript append failed"});
        transcript_scratch.push_back(std::move(asst_evt));
        Message assistant{"assistant", resp.text, "", ""};
        std::vector<std::optional<PreparedToolCall>> prepared(calls.size());
        std::vector<std::optional<ToolResult>> results(calls.size());
        bool parallel = calls.size() > 1 && tool_executor_.worker_count() > 1;
        for (size_t i = 0; i < calls.size(); ++i) {
            const auto& call = calls[i];
            auto event =
                make_transcript_tool_call(turn_id, call.call_id, call.name, call.arguments);
            event["provider_call_id"] = resp.tool_calls[i].call_id;
            redact_json(event, cfg_);
            if (!session.transcript.append(event))
                return std::unexpected(RuntimeError{"Transcript append failed"});
            assistant.tool_calls.push_back(
                {resp.tool_calls[i].call_id, call.name, event["arguments"]});
            transcript_scratch.push_back(std::move(event));
            const auto* def = tools_->definition(call.name);
            std::string rejection;
            if (!def) {
                rejection = "Unknown tool: " + call.name;
            } else if (auto decision = policy_->check(*def); !decision) {
                rejection = decision.error().reason;
            } else {
                auto validated = tools_->prepare(call);
                if (validated)
                    prepared[i] = std::move(*validated);
                else
                    rejection = validated.error();
            }
            if (!rejection.empty())
                results[i] = ToolResult{call.call_id, false, nullptr, std::move(rejection)};
            parallel &= prepared[i].has_value() && prepared[i]->read_only();
        }
        req.messages.push_back(std::move(assistant));
        const ToolContext context{session.workspace, &cfg_.limits, stop,
                                  cfg_.tools.allow_overwrite};
        auto execute = [&](size_t index) {
            if (!prepared[index])
                return;
            // Policy is immutable after runtime construction; path access remains tool-owned
            // and is checked at the effect, after preparation and any queue wait.
            auto executed = tools_->execute(*prepared[index], context);
            results[index] =
                executed ? std::move(*executed)
                         : ToolResult{calls[index].call_id, false, nullptr, executed.error()};
        };
        bool stalled = false;
        auto finish = [&](size_t index) -> std::expected<void, RuntimeError> {
            auto& result = *results[index];
            const auto& call = calls[index];
            const auto decision = loop_guard.observe(
                call, result, prepared[index].has_value() && prepared[index]->read_only());
            if (decision == ToolLoopGuard::Decision::Warn)
                result.error +=
                    " [loop warning: repeated unchanged outcome; choose a different action]";
            stalled |= decision == ToolLoopGuard::Decision::Stop;
            redact_json(result.content, cfg_);
            result.error = redact(std::move(result.error), cfg_);
            if (call.name == "read_file" && result.content.is_object() &&
                result.content.contains("next_offset") &&
                to_json_string(result.content).size() > cfg_.limits.max_tool_result_bytes) {
                result.ok = false;
                result.content = nullptr;
                result.error =
                    "Read page exceeds budget after redaction; retry with a smaller limit";
            }
            result.content =
                bound_tool_content(std::move(result.content), cfg_.limits.max_tool_result_bytes);
            auto event = make_transcript_tool_result(turn_id, call.call_id, result.ok,
                                                     result.content, result.error);
            event["provider_call_id"] = redact(resp.tool_calls[index].call_id, cfg_);
            // Redaction runs before sizing; avoid a second pass changing bounded output.
            if (!session.transcript.append(event))
                return std::unexpected(RuntimeError{"Transcript append failed"});
            transcript_scratch.push_back(std::move(event));
            req.messages.push_back(
                {"tool",
                 to_json_string(JsonValue{
                     {"ok", result.ok}, {"content", result.content}, {"error", result.error}}),
                 resp.tool_calls[index].call_id,
                 call.name,
                 {},
                 !result.ok});
            emit(make_tool_end(call.name, call.call_id, result.ok, result.error));
            ++tool_call_count;
            return {};
        };
        // If submission or persistence fails, settle every started worker before turn-local
        // arguments/results/context go out of scope. Futures from packaged_task don't join.
        struct Pending {
            std::vector<std::future<void>> tasks;
            ~Pending() {
                for (auto& task : tasks)
                    if (task.valid())
                        task.wait();
            }
        } pending;
        pending.tasks.reserve(calls.size());
        if (parallel) {
            for (size_t i = 0; i < calls.size(); ++i) {
                emit(make_tool_start(calls[i].name, calls[i].call_id));
                pending.tasks.push_back(tool_executor_.submit([&, i] { execute(i); }));
            }
        }
        for (size_t i = 0; i < calls.size(); ++i) {
            if (parallel) {
                pending.tasks[i].get();
            } else {
                emit(make_tool_start(calls[i].name, calls[i].call_id));
                if (stalled)
                    results[i] = ToolResult{calls[i].call_id, false, nullptr,
                                            "Tool skipped after loop detection"};
                else
                    execute(i);
            }
            auto saved = finish(i);
            if (!saved)
                return std::unexpected(saved.error());
        }
        if (stop.stop_requested())
            return std::unexpected(RuntimeError{"Turn cancelled", true});
        if (stalled)
            return std::unexpected(
                RuntimeError{"Tool loop stopped after repeated unchanged outcomes"});

        // Model needs to continue after tool results.
        return true;
    }

    // No tool calls: this is the final assistant answer.
    final_text_out = resp.text;
    auto final_evt = make_transcript_assistant(turn_id, resp.text);
    if (!session.transcript.append(final_evt))
        return std::unexpected(RuntimeError{"Transcript append failed"});

    return false;
}

std::expected<TurnOutput, RuntimeError> AgentRuntime::run_turn(const TurnInput& input,
                                                               std::stop_token stop) {
    if (input.session_id.empty())
        return std::unexpected(RuntimeError{"session_id is required"});
    if (input.message.empty())
        return std::unexpected(RuntimeError{"message is required"});

    // Determine workspace.
    std::filesystem::path ws = input.workspace.empty()
                                   ? std::filesystem::path(cfg_.runtime.default_workspace)
                                   : std::filesystem::path(input.workspace);

    auto session_result = sessions_.get_or_create(input.session_id, ws);
    if (!session_result)
        return std::unexpected(RuntimeError{session_result.error().message});

    SessionState& session = **session_result;
    std::string turn_id = generate_turn_id();

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(cfg_.limits.max_turn_seconds);
    const auto cancelled = [&] {
        return stop.stop_requested() || std::chrono::steady_clock::now() >= deadline;
    };
    std::unique_lock<std::mutex> lock(session.turn_mutex, std::defer_lock);
    while (!lock.try_lock()) {
        if (cancelled())
            return std::unexpected(RuntimeError{"Turn cancelled while waiting for session", true});
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    while (true) {
        if (cancelled())
            return std::unexpected(RuntimeError{"Turn cancelled while waiting for capacity", true});
        {
            std::lock_guard active_lock(active_mutex_);
            if (active_turns_ < cfg_.limits.max_concurrent_sessions) {
                ++active_turns_;
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    struct ActiveTurn {
        std::mutex& mutex;
        size_t& count;
        ~ActiveTurn() {
            std::lock_guard active_lock(mutex);
            --count;
        }
    } active_turn{active_mutex_, active_turns_};

    // At most max_concurrent_sessions deadline threads exist, all joined by RAII.
    std::stop_source turn_stop;
    std::stop_callback external_stop(stop, [&] { turn_stop.request_stop(); });
    std::mutex timer_mutex;
    std::condition_variable_any timer_cv;
    std::jthread timer([&](std::stop_token timer_stop) {
        std::unique_lock timer_lock(timer_mutex);
        timer_cv.wait_until(timer_lock, timer_stop, deadline, [] { return false; });
        if (!timer_stop.stop_requested())
            turn_stop.request_stop();
    });
    stop = turn_stop.get_token();

    emit(make_lifecycle_event(input.session_id, turn_id, PHASE_START));
    struct EndEvent {
        std::function<void()> finish;
        ~EndEvent() {
            finish();
        }
    } end_event{[&] { emit(make_lifecycle_event(input.session_id, turn_id, PHASE_END)); }};

    auto fail = [&](RuntimeError error) -> std::expected<TurnOutput, RuntimeError> {
        emit(make_error_event(error.message, input.session_id, turn_id));
        auto saved = session.transcript.append(make_transcript_error(turn_id, error.message));
        if (!saved)
            error.message += "; error persistence failed";
        return std::unexpected(std::move(error));
    };
    if (stop.stop_requested())
        return fail({"Turn cancelled", true});

    auto transcript_size = session.transcript.byte_size();
    if (!transcript_size)
        return fail({"Cannot read transcript size"});
    if (*transcript_size > cfg_.limits.max_transcript_bytes) {
        // Deterministic, bounded fallback; no extra model call or network traffic.
        auto summary = memory_->load_summary(session.session_id);
        if (!summary)
            return fail({"Cannot load compaction summary"});
        std::string text =
            summary->has_value() ? utf8_prefix(**summary, 2048) : "Session " + session.session_id;
        text += "\nRecent user goals:\n";
        auto goals =
            session.transcript.load_recent(std::min<size_t>(cfg_.limits.max_prompt_bytes, 16384));
        if (!goals)
            return fail({"Cannot load compaction context"});
        for (const auto& event : *goals) {
            if (event.is_object() && event.value("type", "") == TEVT_USER_MESSAGE)
                text += utf8_prefix(event.value("text", ""), 256) + "\n";
            if (text.size() >= 4096)
                break;
        }
        text = redact(utf8_prefix(text, 4096), cfg_);
        if (!memory_->save_summary(session.session_id, text))
            return fail({"Cannot save compaction summary"});
        if (!session.transcript.compact(text, std::min<size_t>(cfg_.limits.max_transcript_bytes / 2,
                                                               cfg_.limits.max_prompt_bytes)))
            return fail({"Cannot compact transcript"});
    }
    auto recent = session.transcript.load_recent(cfg_.limits.max_prompt_bytes);
    if (!recent)
        return fail({"Failed to load transcript"});
    auto user_evt = make_transcript_user(turn_id, redact(input.message, cfg_));
    if (!session.transcript.append(user_evt))
        return fail({"Transcript append failed"});
    std::array<std::byte, 16384> arena_buffer{};
    std::pmr::monotonic_buffer_resource arena(arena_buffer.data(), arena_buffer.size());
    std::pmr::vector<JsonValue> turn_events(&arena);
    turn_events.reserve(recent->size() + 1);
    for (auto& event : *recent)
        turn_events.push_back(std::move(event));
    recent->clear();
    turn_events.push_back(std::move(user_evt));

    std::string final_text;
    int tool_call_count = 0;
    bool finished = false;
    try {
        auto summary = memory_->load_summary(session.session_id);
        if (!summary)
            return fail({"Failed to load memory summary"});
        PromptBuilderOptions options;
        options.max_prompt_bytes = cfg_.limits.max_prompt_bytes;
        options.prepared_tool_schemas = &allowed_tool_schemas_;
        if (summary->has_value())
            options.memory_summary = **summary;
        auto request = prompt_builder_.build(turn_events, {}, allowed_tools_, options);
        ToolLoopGuard loop_guard;
        for (int i = 0; i < 16; ++i) {
            if (stop.stop_requested())
                return fail({"Turn cancelled or deadline exceeded", true});
            auto result = model_iteration(session, turn_id, turn_events, final_text,
                                          tool_call_count, request, options, loop_guard, stop);
            if (!result)
                return fail(result.error());
            if (!*result) {
                finished = true;
                break;
            }
        }
    } catch (const std::length_error&) {
        return fail({"Current turn exceeds prompt byte budget"});
    } catch (const std::exception&) {
        return fail({"Turn failed while processing model or tool data"});
    }
    if (!finished)
        return fail({"Model iteration limit exceeded"});

    emit(make_assistant_final(final_text, input.session_id, turn_id));

    TurnOutput out;
    out.session_id = input.session_id;
    out.turn_id = turn_id;
    out.final_text = final_text;
    out.had_tool_calls = tool_call_count > 0;
    out.tool_call_count = tool_call_count;

    return out;
}

std::unique_ptr<AgentRuntime> make_default_runtime(Config cfg) {
    auto tools = std::make_unique<ToolRegistry>();
    tools->register_tool(make_echo_tool());
    tools->register_tool(make_list_dir_tool());
    tools->register_tool(make_read_file_tool());
    tools->register_tool(make_write_file_tool());
    tools->register_tool(make_shell_tool());

    auto policy = std::make_unique<ToolPolicy>(cfg.tools);
    auto memory = std::make_unique<MemoryManager>(cfg.runtime.data_dir);

    std::unique_ptr<IModelClient> client;
    if (cfg.model.provider == "anthropic") {
        client = make_anthropic_client();
    } else {
        // generic_http defaults to anthropic-compatible for now;
        // OpenAI-compatible endpoint can be selected via provider="openai"
        client = cfg.model.provider == "openai" ? make_openai_client() : make_anthropic_client();
    }

    return std::make_unique<AgentRuntime>(std::move(cfg), std::move(client), std::move(tools),
                                          std::move(policy), std::move(memory));
}

} // namespace agent
