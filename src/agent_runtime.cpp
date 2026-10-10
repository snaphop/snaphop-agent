#include "agent/agent_runtime.hpp"
#include "agent/event.hpp"
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
      model_client_(std::move(model_client)), tools_(std::move(tools)), policy_(std::move(policy)) {
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

static std::string utf8_prefix(std::string_view text, size_t limit) {
    size_t end = std::min(text.size(), limit);
    while (end < text.size() && end > 0 && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
        --end;
    return std::string(text.substr(0, end));
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
                              std::string& final_text_out, int& tool_call_count,
                              std::stop_token stop) {
    // Persisted history is loaded once per turn; append events to this bounded turn buffer.
    std::string memory_summary;
    auto summary = memory_->load_summary(session.session_id);
    if (!summary)
        return std::unexpected(RuntimeError{"Failed to load memory summary"});
    if (summary->has_value())
        memory_summary = **summary;

    // Get tool definitions that pass policy.
    auto all_defs = tools_->definitions();
    std::vector<ToolDefinition> allowed_defs;
    for (const auto& def : all_defs) {
        if (policy_->is_allowed(def))
            allowed_defs.push_back(def);
    }

    PromptBuilderOptions opts;
    opts.max_prompt_bytes = cfg_.limits.max_prompt_bytes;
    opts.memory_summary = memory_summary;

    auto req = prompt_builder_.build(transcript_scratch, {}, allowed_defs, opts);

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
        auto asst_evt = make_transcript_assistant(turn_id, resp.text);
        auto saved = session.transcript.append(asst_evt);
        if (!saved)
            return std::unexpected(RuntimeError{"Transcript append failed"});
        transcript_scratch.push_back(asst_evt);

        for (const auto& mtc : resp.tool_calls) {
            ToolCall tc{
                .call_id = mtc.call_id.empty() ? generate_call_id() : mtc.call_id,
                .name = mtc.name,
                .arguments = mtc.arguments,
            };

            if (stop.stop_requested())
                return std::unexpected(RuntimeError{"Turn cancelled", true});
            emit(make_tool_start(tc.name, tc.call_id));

            // Persist tool call in transcript.
            auto tc_evt = make_transcript_tool_call(turn_id, tc.call_id, tc.name, tc.arguments);
            redact_json(tc_evt, cfg_);
            if (!session.transcript.append(tc_evt))
                return std::unexpected(RuntimeError{"Transcript append failed"});
            transcript_scratch.push_back(tc_evt);

            // Policy check.
            ITool* tool = tools_->find(tc.name);
            ToolDefinition def_for_policy;
            if (tool)
                def_for_policy = tool->definition();
            else
                def_for_policy = {
                    .name = tc.name, .description = "", .params = {}, .dangerous = false};

            auto policy_result = policy_->check(def_for_policy);

            ToolResult result;
            if (!policy_result) {
                result = {.call_id = tc.call_id,
                          .ok = false,
                          .content = nullptr,
                          .error = policy_result.error().reason};
            } else {
                ToolContext ctx{
                    .workspace = session.workspace,
                    .limits = &cfg_.limits,
                    .stop = stop,
                    .allow_overwrite = cfg_.tools.allow_overwrite,
                };
                auto exec = tools_->execute(tc, ctx);
                if (exec) {
                    result = std::move(*exec);
                } else {
                    result = {.call_id = tc.call_id,
                              .ok = false,
                              .content = nullptr,
                              .error = exec.error()};
                }
            }

            redact_json(result.content, cfg_);
            result.error = redact(std::move(result.error), cfg_);
            // Truncate tool result content if too large.
            std::string content_str = to_json_string(result.content);
            if (content_str.size() > cfg_.limits.max_tool_result_bytes) {
                result.content = "[tool result truncated]";
                if (to_json_string(result.content).size() > cfg_.limits.max_tool_result_bytes)
                    result.content = 0;
            }

            emit(make_tool_end(tc.name, tc.call_id, result.ok, result.error));

            // Persist tool result.
            auto tr_evt = make_transcript_tool_result(turn_id, tc.call_id, result.ok,
                                                      result.content, result.error);
            redact_json(tr_evt, cfg_);
            if (!session.transcript.append(tr_evt))
                return std::unexpected(RuntimeError{"Transcript append failed"});
            transcript_scratch.push_back(tr_evt);

            tool_call_count++;
        }

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
        for (int i = 0; i < 16; ++i) {
            if (stop.stop_requested())
                return fail({"Turn cancelled or deadline exceeded", true});
            auto result =
                model_iteration(session, turn_id, turn_events, final_text, tool_call_count, stop);
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
