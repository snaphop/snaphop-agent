#include "agent/prompt_builder.hpp"
#include "agent/event.hpp"
#include <sstream>
#include <algorithm>

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
                if (p.required) ss << ", required";
                ss << "): " << p.description << "\n";
            }
        }
    }
    return ss.str();
}

std::string PromptBuilder::build_system_prompt(
    const std::vector<ToolDefinition>& tools,
    std::string_view memory_summary,
    bool include_tools) const
{
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
static std::vector<Message> events_to_messages(const std::vector<JsonValue>& events) {
    std::vector<Message> msgs;
    for (const auto& evt : events) {
        if (!evt.is_object()) continue;
        auto type = evt.value("type", "");

        if (type == std::string(TEVT_USER_MESSAGE)) {
            msgs.push_back({"user", evt.value("text", ""), "", ""});
        } else if (type == std::string(TEVT_ASSISTANT_MESSAGE)) {
            msgs.push_back({"assistant", evt.value("text", ""), "", ""});
        } else if (type == std::string(TEVT_TOOL_CALL)) {
            std::string summary = "[tool call: " + evt.value("tool", "") + "]";
            msgs.push_back({"assistant", summary, "", ""});
        } else if (type == std::string(TEVT_TOOL_RESULT)) {
            std::string content = evt.contains("content") ? to_json_string(evt["content"]) : "";
            msgs.push_back({"user", "[tool result]\n" + content, evt.value("call_id", ""), ""});
        }
        // compaction_summary, system_note, error: skip in message replay
    }
    return msgs;
}

ModelRequest PromptBuilder::build(
    const std::vector<JsonValue>& transcript_events,
    std::string_view user_message,
    const std::vector<ToolDefinition>& available_tools,
    const PromptBuilderOptions& opts) const
{
    size_t budget = std::min(opts.max_prompt_bytes, cfg_.limits.max_prompt_bytes);

    std::vector<ToolDefinition> tools;
    if (opts.include_tool_definitions) tools = available_tools;

    ModelRequest req;
    req.system_prompt = build_system_prompt(tools, opts.memory_summary, opts.include_tool_definitions);
    req.tools = tools;
    req.model = cfg_.model.model;
    req.max_output_tokens = 4096;

    // Build history from transcript, respecting byte budget.
    size_t used = req.system_prompt.size() + user_message.size();
    auto history = events_to_messages(transcript_events);

    // Trim from the front until we fit in budget.
    while (!history.empty() && used + history.front().content.size() > budget) {
        history.erase(history.begin());
    }

    req.messages = std::move(history);
    req.messages.push_back({"user", std::string(user_message), "", ""});

    // API credentials resolved from env at call time.
    const char* key = std::getenv(cfg_.model.api_key_env.c_str());
    const char* ep  = std::getenv(cfg_.model.endpoint_env.c_str());
    req.api_key  = key ? key : "";
    req.endpoint = ep  ? ep  : "";

    return req;
}

} // namespace agent
