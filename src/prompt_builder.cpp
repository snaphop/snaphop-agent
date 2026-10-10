#include "agent/prompt_builder.hpp"
#include "agent/event.hpp"
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
        ss << "\n" << format_tool_definitions(tools);
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
            it->tool_calls.push_back({evt.value("call_id", ""), evt.value("tool", ""),
                                      evt.value("arguments", JsonValue::object())});
        } else if (type == TEVT_TOOL_RESULT) {
            std::string content =
                to_json_string(JsonValue{{"ok", evt.value("ok", false)},
                                         {"content", evt.value("content", JsonValue(nullptr))},
                                         {"error", evt.value("error", "")}});
            msgs.push_back({"tool",
                            std::move(content),
                            evt.value("call_id", ""),
                            "",
                            {},
                            !evt.value("ok", false)});
        }
    }
    // Tail loading may start inside a tool exchange. Keep complete user turns only.
    const auto first_user =
        std::find_if(msgs.begin(), msgs.end(), [](const auto& m) { return m.role == "user"; });
    msgs.erase(msgs.begin(), first_user);
    // Interrupted tool batches cannot be replayed as native calls with missing results.
    for (size_t i = 0; i < msgs.size(); ++i) {
        if (msgs[i].tool_calls.empty())
            continue;
        std::unordered_set<std::string> results;
        size_t end = i + 1;
        while (end < msgs.size() && msgs[end].role == "tool")
            results.insert(msgs[end++].tool_call_id);
        bool complete = results.size() == msgs[i].tool_calls.size();
        for (const auto& call : msgs[i].tool_calls)
            complete &= results.contains(call.call_id);
        if (!complete) {
            msgs[i].tool_calls.clear();
            msgs[i].content += " [interrupted tool exchange]";
            msgs.erase(msgs.begin() + i + 1, msgs.begin() + end);
        }
    }
    return msgs;
}

ModelRequest PromptBuilder::build(std::span<const JsonValue> transcript_events,
                                  std::string_view user_message,
                                  const std::vector<ToolDefinition>& available_tools,
                                  const PromptBuilderOptions& opts) const {
    size_t budget = std::min(opts.max_prompt_bytes, cfg_.limits.max_prompt_bytes);

    std::vector<ToolDefinition> tools;
    if (opts.include_tool_definitions)
        tools = available_tools;

    ModelRequest req;
    req.system_prompt =
        build_system_prompt(tools, opts.memory_summary, opts.include_tool_definitions);
    req.tools = tools;
    req.model = cfg_.model.model;
    req.max_output_tokens = 4096;

    req.messages = events_to_messages(transcript_events);
    if (!user_message.empty())
        req.messages.push_back({"user", std::string(user_message), "", ""});

    // Account for JSON escaping, native calls, and tool schemas in the actual wire body.
    // Remove whole older turns so a tool result never loses its assistant call.
    auto wire_size = [&] {
        return cfg_.model.provider == "openai" ? to_json_string(openai_request_body(req)).size()
                                               : to_json_string(anthropic_request_body(req)).size();
    };
    while (wire_size() > budget && !req.messages.empty()) {
        auto next = std::find_if(req.messages.begin() + 1, req.messages.end(),
                                 [](const auto& m) { return m.role == "user"; });
        if (next == req.messages.end())
            break;
        req.messages.erase(req.messages.begin(), next);
    }
    if (wire_size() > budget && !opts.memory_summary.empty())
        req.system_prompt = build_system_prompt(tools, {}, opts.include_tool_definitions);
    if (wire_size() > budget)
        throw std::length_error(
            "Prompt budget cannot fit system instructions, tools and current turn");
    req.timeout_seconds = cfg_.limits.max_turn_seconds;

    // API credentials resolved from env at call time.
    const char* key = std::getenv(cfg_.model.api_key_env.c_str());
    const char* ep = std::getenv(cfg_.model.endpoint_env.c_str());
    req.api_key = key ? key : "";
    req.endpoint = ep ? ep : "";

    return req;
}

} // namespace agent
