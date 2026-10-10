#pragma once
#include <span>

#include "config.hpp"
#include "tool.hpp"
#include "model_client.hpp"
#include "transcript_store.hpp"
#include <string>
#include <vector>
#include <optional>

namespace agent {

struct PromptBuilderOptions {
    size_t max_prompt_bytes = 262144;
    std::string memory_summary; // pre-loaded memory summary, if any
    bool include_tool_definitions = true;
    // Optional immutable cache owned by the runtime; borrowed only during build().
    const JsonValue* prepared_tool_schemas = nullptr;
};

// Builds the full ModelRequest from session state, memory, and the current user message.
class PromptBuilder {
  public:
    explicit PromptBuilder(const Config& cfg);

    // Build a ModelRequest for the current turn.
    // transcript_events should be the recent events from load_recent().
    ModelRequest build(std::span<const JsonValue> transcript_events, std::string_view user_message,
                       const std::vector<ToolDefinition>& available_tools,
                       const PromptBuilderOptions& opts = {}) const;

    // Fit an incrementally extended request without rebuilding stable context.
    void fit(ModelRequest& request, const PromptBuilderOptions& opts = {}) const;

    // Estimate approximate token count from byte count (rough heuristic: 1 token ≈ 4 bytes).
    static size_t estimate_tokens(size_t bytes) {
        return bytes / 4;
    }

    // Serialize tool definitions to a compact JSON-like string for the system prompt.
    static std::string format_tool_definitions(const std::vector<ToolDefinition>& tools);

  private:
    std::string build_system_prompt(const std::vector<ToolDefinition>& tools,
                                    std::string_view memory_summary, bool include_tools) const;

    Config cfg_;
};

} // namespace agent
