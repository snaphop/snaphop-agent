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

namespace agent {

// External factory declarations for tools.
std::unique_ptr<ITool> make_echo_tool();
std::unique_ptr<ITool> make_list_dir_tool();
std::unique_ptr<ITool> make_read_file_tool();
std::unique_ptr<ITool> make_write_file_tool();
std::unique_ptr<ITool> make_shell_tool();

AgentRuntime::AgentRuntime(
    Config cfg,
    std::unique_ptr<IModelClient> model_client,
    std::unique_ptr<ToolRegistry> tools,
    std::unique_ptr<ToolPolicy> policy,
    std::unique_ptr<MemoryManager> memory)
    : cfg_(std::move(cfg))
    , sessions_(cfg_)
    , prompt_builder_(cfg_)
    , memory_(cfg_.runtime.data_dir)
    , model_client_(std::move(model_client))
    , tools_(std::move(tools))
    , policy_(std::move(policy))
{
    (void)memory; // memory_ constructed from cfg above
}

static std::string make_random_id(std::string_view prefix) {
    static std::atomic<uint64_t> counter{0};
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    auto n   = counter.fetch_add(1, std::memory_order_relaxed);
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
AgentRuntime::model_iteration(
    SessionState& session,
    const std::string& turn_id,
    std::vector<JsonValue>& transcript_scratch,
    std::string& final_text_out,
    int& tool_call_count,
    std::stop_token stop)
{
    // Load recent transcript for context.
    auto recent = session.transcript.load_recent(cfg_.limits.max_prompt_bytes / 2);
    if (!recent)
        return std::unexpected(RuntimeError{"Failed to load transcript: " + recent.error().message});

    // Load memory summary if available.
    std::string memory_summary;
    auto summary = memory_.load_summary(session.session_id);
    if (summary && summary->has_value()) memory_summary = **summary;

    // Get tool definitions that pass policy.
    auto all_defs = tools_->definitions();
    std::vector<ToolDefinition> allowed_defs;
    for (const auto& def : all_defs) {
        if (policy_->is_allowed(def)) allowed_defs.push_back(def);
    }

    PromptBuilderOptions opts;
    opts.max_prompt_bytes = cfg_.limits.max_prompt_bytes;
    opts.memory_summary   = memory_summary;

    // Combine persisted recent events with in-turn scratch (tool results).
    auto combined = *recent;
    combined.insert(combined.end(), transcript_scratch.begin(), transcript_scratch.end());

    // The last user message was already appended to transcript; use empty string here
    // since the last item in combined is already the user message.
    // We extract it and pass it separately so PromptBuilder can place it last.
    std::string user_msg_text;
    std::vector<JsonValue> history = combined;
    if (!history.empty()) {
        const auto& last = history.back();
        if (last.is_object() && last.value("type", "") == std::string(TEVT_USER_MESSAGE)) {
            user_msg_text = last.value("text", "");
            history.pop_back();
        }
    }

    auto req = prompt_builder_.build(history, user_msg_text, allowed_defs, opts);

    // Call the model.
    auto delta_cb = [](std::string_view chunk) -> bool {
        emit_event(make_assistant_delta(chunk));
        return true;
    };

    auto resp = model_client_->complete(req, stop, delta_cb);

    if (stop.stop_requested()) {
        return std::unexpected(RuntimeError{"Turn cancelled", true});
    }

    if (resp.finish_reason == FinishReason::Error) {
        return std::unexpected(RuntimeError{resp.error_message, true});
    }

    // Handle tool calls.
    if (!resp.tool_calls.empty()) {
        // Persist the assistant message with tool intent.
        std::string assistant_note;
        for (const auto& tc : resp.tool_calls) {
            assistant_note += "[calling " + tc.name + "] ";
        }
        if (!resp.text.empty()) assistant_note = resp.text + "\n" + assistant_note;

        auto asst_evt = make_transcript_assistant(turn_id, assistant_note);
        session.transcript.append(asst_evt);
        transcript_scratch.push_back(asst_evt);

        for (const auto& mtc : resp.tool_calls) {
            ToolCall tc{
                .call_id   = mtc.call_id.empty() ? generate_call_id() : mtc.call_id,
                .name      = mtc.name,
                .arguments = mtc.arguments,
            };

            emit_event(make_tool_start(tc.name, tc.call_id));

            // Persist tool call in transcript.
            auto tc_evt = make_transcript_tool_call(turn_id, tc.call_id, tc.name, tc.arguments);
            session.transcript.append(tc_evt);
            transcript_scratch.push_back(tc_evt);

            // Policy check.
            ITool* tool = tools_->find(tc.name);
            ToolDefinition def_for_policy;
            if (tool) def_for_policy = tool->definition();
            else      def_for_policy = {.name = tc.name, .description = "", .params = {}, .dangerous = false};

            auto policy_result = policy_->check(def_for_policy);

            ToolResult result;
            if (!policy_result) {
                result = {.call_id = tc.call_id, .ok = false,
                          .content = nullptr, .error = policy_result.error().reason};
            } else {
                ToolContext ctx{
                    .workspace = session.workspace,
                    .limits    = &cfg_.limits,
                };
                auto exec = tools_->execute(tc, ctx);
                if (exec) {
                    result = std::move(*exec);
                } else {
                    result = {.call_id = tc.call_id, .ok = false,
                              .content = nullptr, .error = exec.error()};
                }
            }

            // Truncate tool result content if too large.
            std::string content_str = to_json_string(result.content);
            if (content_str.size() > cfg_.limits.max_tool_result_bytes) {
                content_str = content_str.substr(0, cfg_.limits.max_tool_result_bytes);
                content_str += "... [truncated]";
                result.content = content_str;
            }

            emit_event(make_tool_end(tc.name, tc.call_id, result.ok, result.error));

            // Persist tool result.
            auto tr_evt = make_transcript_tool_result(turn_id, tc.call_id, result.ok, result.content, result.error);
            session.transcript.append(tr_evt);
            transcript_scratch.push_back(tr_evt);

            tool_call_count++;
        }

        // Model needs to continue after tool results.
        return true;
    }

    // No tool calls: this is the final assistant answer.
    final_text_out = resp.text;
    auto final_evt = make_transcript_assistant(turn_id, resp.text);
    session.transcript.append(final_evt);

    return false;
}

std::expected<TurnOutput, RuntimeError>
AgentRuntime::run_turn(const TurnInput& input, std::stop_token stop) {
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
    std::string turn_id   = generate_turn_id();

    // Serialize turns within a session.
    std::unique_lock<std::mutex> lock(session.turn_mutex);

    emit_event(make_lifecycle_event(input.session_id, turn_id, PHASE_START));

    // Persist the user message.
    auto user_evt = make_transcript_user(turn_id, input.message);
    auto append_result = session.transcript.append(user_evt);
    if (!append_result)
        return std::unexpected(RuntimeError{"Transcript append failed: " + append_result.error().message});

    // In-turn scratch events (tool calls + results added mid-turn).
    std::vector<JsonValue> transcript_scratch;
    // Include the user message we just appended so the next load_recent sees it.
    transcript_scratch.push_back(user_evt);

    std::string final_text;
    int tool_call_count = 0;
    int max_iterations  = 16; // guard against runaway tool loops

    for (int i = 0; i < max_iterations; ++i) {
        if (stop.stop_requested()) break;
        auto result = model_iteration(
            session, turn_id, transcript_scratch, final_text, tool_call_count, stop);

        if (!result) {
            emit_event(make_error_event(result.error().message, input.session_id, turn_id));
            session.transcript.append(make_transcript_error(turn_id, result.error().message));
            return std::unexpected(result.error());
        }

        if (!*result) break; // no more tool calls; final answer received
    }

    emit_event(make_assistant_final(final_text, input.session_id, turn_id));
    emit_event(make_lifecycle_event(input.session_id, turn_id, PHASE_END));

    TurnOutput out;
    out.session_id      = input.session_id;
    out.turn_id         = turn_id;
    out.final_text      = final_text;
    out.had_tool_calls  = tool_call_count > 0;
    out.tool_call_count = tool_call_count;

    return out;
}

std::unique_ptr<AgentRuntime> make_default_runtime(Config cfg) {
    auto tools   = std::make_unique<ToolRegistry>();
    tools->register_tool(make_echo_tool());
    tools->register_tool(make_list_dir_tool());
    tools->register_tool(make_read_file_tool());
    tools->register_tool(make_write_file_tool());
    tools->register_tool(make_shell_tool());

    auto policy  = std::make_unique<ToolPolicy>(cfg.tools);
    auto memory  = std::make_unique<MemoryManager>(cfg.runtime.data_dir);

    std::unique_ptr<IModelClient> client;
    if (cfg.model.provider == "anthropic") {
        client = make_anthropic_client();
    } else {
        // generic_http defaults to anthropic-compatible for now;
        // OpenAI-compatible endpoint can be selected via provider="openai"
        client = cfg.model.provider == "openai"
            ? make_openai_client()
            : make_anthropic_client();
    }

    return std::make_unique<AgentRuntime>(
        std::move(cfg),
        std::move(client),
        std::move(tools),
        std::move(policy),
        std::move(memory));
}

} // namespace agent
