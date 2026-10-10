#include "agent/prompt_builder.hpp"
#include "agent/event.hpp"
#include "agent/tool_result_budget.hpp"
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace agent {

PromptBuilder::PromptBuilder(const Config& cfg) : cfg_(cfg) {}

std::string PromptBuilder::format_tool_definitions(const std::vector<ToolDefinition>& tools) {
    std::ostringstream ss;
    ss << "Available tools:\n";
    for (const auto& t : tools) {
        ss << "\n## " << t.name << "\n";
        ss << t.description << "\n";
        if (!t.params.empty()) {
            ss << "Parameters:\n";
            for (const auto& p : t.params) {
                ss << "  - " << p.name << " (" << p.type;
                if (p.required)
                    ss << ", required";
                ss << "): " << p.description << "\n";
            }
        }
    }
    return ss.str();
}

std::string PromptBuilder::build_system_prompt(const std::vector<ToolDefinition>& tools,
                                               std::string_view memory_summary,
                                               bool include_tools) const {
    std::ostringstream ss;
    ss << "You are a helpful AI agent with access to file system tools.\n\n"
       << "Rules:\n"
       << "- Use tools only when needed to answer the user's request.\n"
       << "- All file paths must be workspace-relative (no absolute paths, no '..' escape).\n"
       << "- Provide brief, direct answers.\n"
       << "- Do not reveal API keys, secrets, or sensitive configuration values.\n"
       << "- When you need to take an action, return a structured tool call.\n";

    if (!memory_summary.empty()) {
        ss << "\n## Memory Summary\n" << memory_summary << "\n";
    }

    if (include_tools && !tools.empty()) {
        ss << "\nAvailable tools (schemas describe parameters):";
        for (const auto& tool : tools)
            ss << " " << tool.name;
        ss << "\n";
    }

    return ss.str();
}

// Convert transcript events to model messages.
static std::vector<Message> events_to_messages(std::span<const JsonValue> events) {
    std::vector<Message> msgs;
    for (const auto& evt : events) {
        if (!evt.is_object())
            continue;
        const auto type = evt.value("type", "");
        if (type == TEVT_USER_MESSAGE) {
            msgs.push_back({"user", evt.value("text", ""), "", ""});
        } else if (type == TEVT_ASSISTANT_MESSAGE) {
            msgs.push_back({"assistant", evt.value("text", ""), "", ""});
        } else if (type == TEVT_TOOL_CALL) {
            // Legacy logs interleave calls/results. Collect every call in the same assistant batch.
            auto it = std::find_if(msgs.rbegin(), msgs.rend(),
                                   [](const auto& m) { return m.role != "tool"; });
            if (it == msgs.rend() || it->role != "assistant")
                continue;
            it->tool_calls.push_back({evt.value("provider_call_id", evt.value("call_id", "")),
                                      evt.value("tool", ""),
                                      evt.value("arguments", JsonValue::object())});
        } else if (type == TEVT_TOOL_RESULT) {
            std::string content =
                to_json_string(JsonValue{{"ok", evt.value("ok", false)},
                                         {"content", evt.value("content", JsonValue(nullptr))},
                                         {"error", evt.value("error", "")}});
            msgs.push_back({"tool",
                            std::move(content),
                            evt.value("provider_call_id", evt.value("call_id", "")),
                            "",
                            {},
                            !evt.value("ok", false)});
        }
    }
    // Tail loading may start inside a tool exchange. Keep complete user turns only.
    const auto first_user =
        std::find_if(msgs.begin(), msgs.end(), [](const auto& m) { return m.role == "user"; });
    msgs.erase(msgs.begin(), first_user);
    // Preserve completed effects and mark missing outcomes as unknown. Never replay
    // execution here: a cancelled write may have happened before its result was saved.
    std::vector<Message> repaired;
    for (size_t i = 0; i < msgs.size();) {
        auto message = std::move(msgs[i++]);
        if (message.role == "tool")
            continue; // Orphaned result with no assistant frame.
        if (message.tool_calls.empty()) {
            repaired.push_back(std::move(message));
            continue;
        }
        size_t end = i;
        while (end < msgs.size() && msgs[end].role == "tool")
            ++end;
        size_t frame_end = end;
        while (frame_end < msgs.size() && msgs[frame_end].tool_calls.empty())
            ++frame_end;
        std::unordered_set<std::string> ids;
        std::vector<ModelToolCall> calls;
        std::vector<Message> results;
        for (auto& call : message.tool_calls) {
            if (call.call_id.empty() || !ids.insert(call.call_id).second)
                continue;
            auto found =
                std::find_if(msgs.begin() + i, msgs.begin() + frame_end, [&](const auto& result) {
                    return result.role == "tool" && result.tool_call_id == call.call_id;
                });
            if (found != msgs.begin() + frame_end) {
                results.push_back(std::move(*found));
                found->role = "tool";
                found->tool_call_id.clear();
            } else {
                results.push_back({"tool",
                                   to_json_string(JsonValue{
                                       {"ok", false},
                                       {"content", nullptr},
                                       {"error", "Tool outcome unknown after interruption; inspect "
                                                 "state before retrying mutations"}}),
                                   call.call_id,
                                   call.name,
                                   {},
                                   true});
            }
            calls.push_back(std::move(call));
        }
        message.tool_calls = std::move(calls);
        repaired.push_back(std::move(message));
        for (auto& result : results)
            repaired.push_back(std::move(result));
        i = end;
    }
    return repaired;
}

ModelRequest PromptBuilder::build(std::span<const JsonValue> transcript_events,
                                  std::string_view user_message,
                                  const std::vector<ToolDefinition>& available_tools,
                                  const PromptBuilderOptions& opts) const {
    ModelRequest req;
    if (opts.include_tool_definitions)
        req.tools = available_tools;
    if (opts.prepared_tool_schemas && opts.prepared_tool_schemas->size() == req.tools.size())
        req.tool_schemas = *opts.prepared_tool_schemas;
    else
        for (const auto& tool : req.tools)
            req.tool_schemas.push_back(tool_input_schema(tool));
    req.system_prompt =
        build_system_prompt(req.tools, opts.memory_summary, opts.include_tool_definitions);
    req.model = cfg_.model.model;
    req.messages = events_to_messages(transcript_events);
    if (!user_message.empty())
        req.messages.push_back({"user", std::string(user_message), "", ""});
    req.timeout_seconds = cfg_.limits.max_turn_seconds;
    const char* key = std::getenv(cfg_.model.api_key_env.c_str());
    const char* endpoint = std::getenv(cfg_.model.endpoint_env.c_str());
    req.api_key = key ? key : "";
    req.endpoint = endpoint ? endpoint : "";
    fit(req, opts);
    return req;
}

void PromptBuilder::fit(ModelRequest& req, const PromptBuilderOptions& opts) const {
    const size_t budget = std::min(opts.max_prompt_bytes, cfg_.limits.max_prompt_bytes);
    // Account for JSON escaping, native calls, and tool schemas in the actual wire body.
    // Remove whole older turns so a tool result never loses its assistant call.
    auto wire_size = [&] {
        return cfg_.model.provider == "openai" ? to_json_string(openai_request_body(req)).size()
                                               : to_json_string(anthropic_request_body(req)).size();
    };
    size_t bytes = wire_size();
    while (bytes > budget && !req.messages.empty()) {
        auto next = std::find_if(req.messages.begin() + 1, req.messages.end(),
                                 [](const auto& m) { return m.role == "user"; });
        if (next == req.messages.end())
            break;
        req.messages.erase(req.messages.begin(), next);
        bytes = wire_size();
    }
    if (bytes > budget && !opts.memory_summary.empty()) {
        req.system_prompt = build_system_prompt(req.tools, {}, opts.include_tool_definitions);
        bytes = wire_size();
    }
    // Context pressure within a single turn cannot be solved by dropping older turns.
    // Reduce older tool payloads in the model view only; keep calls/results paired and
    // retain the persisted evidence. Leave the newest result intact whenever possible.
    if (bytes > budget) {
        const size_t tool_count =
            static_cast<size_t>(std::count_if(req.messages.begin(), req.messages.end(),
                                              [](const auto& m) { return m.role == "tool"; }));
        const size_t share = std::max<size_t>(32, budget / 4 / std::max<size_t>(1, tool_count));
        for (auto& message : req.messages) {
            if (message.role != "tool" || message.content.size() <= share)
                continue;
            // Preserve the result envelope and its success/error state.
            auto payload = parse_json(message.content);
            if (payload && payload->is_object() && payload->contains("content")) {
                (*payload)["content"] = bound_tool_content(std::move((*payload)["content"]), share);
                message.content = to_json_string(*payload);
            }
            bytes = wire_size();
            if (bytes <= budget)
                break;
        }
    }
    if (bytes > budget)
        throw std::length_error(
            "Prompt budget cannot fit system instructions, tools and current turn");
}

} // namespace agent
