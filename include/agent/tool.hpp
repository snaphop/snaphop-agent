#pragma once

#include "json.hpp"
#include <filesystem>
#include <string>
#include <vector>
#include <memory>
#include <expected>
#include <stop_token>

namespace agent {

struct LimitsConfig;

struct ToolParam {
    std::string name;
    std::string type; // "string", "boolean", "integer", "object", "array"
    bool required;
    std::string description;
};

struct ToolDefinition {
    std::string name;
    std::string description;
    std::vector<ToolParam> params;
    bool dangerous = false;
};

struct ToolCall {
    std::string call_id;
    std::string name;
    JsonValue arguments; // JSON object
};

struct ToolResult {
    std::string call_id;
    bool ok;
    JsonValue content; // output to return to model
    std::string error; // human-readable error if !ok
};

// Context available to tools during execution.
struct ToolContext {
    std::filesystem::path workspace;
    const LimitsConfig* limits = nullptr;
    std::stop_token stop = {};
    bool allow_overwrite = false;
};

// Workspace path safety helper.
struct WorkspacePathError {
    std::string message;
};

// Validate and resolve a workspace-relative path.
// Rejects absolute paths, NUL bytes, and escapes via ../
std::expected<std::filesystem::path, WorkspacePathError>
resolve_workspace_path(const std::filesystem::path& workspace, std::string_view rel_path);

class ITool {
  public:
    virtual ~ITool() = default;
    virtual ToolDefinition definition() const = 0;
    virtual ToolResult call(const ToolCall& tc, const ToolContext& ctx) = 0;
};

// Registry of all known tools.
class ToolRegistry {
  public:
    // Register a tool. Replaces any existing tool with the same name.
    void register_tool(std::unique_ptr<ITool> tool);

    // Find a tool by name. Returns nullptr if not found.
    ITool* find(const std::string& name) const;

    // Return all tool definitions (for model prompt injection).
    std::vector<ToolDefinition> definitions() const;

    // Execute a tool call after validation. Returns error if tool not found or
    // arguments are invalid.
    std::expected<ToolResult, std::string> execute(const ToolCall& tc,
                                                   const ToolContext& ctx) const;

    size_t size() const {
        return tools_.size();
    }

  private:
    // Validate tool call arguments against definition.
    std::expected<void, std::string> validate(const ToolCall& tc, const ToolDefinition& def) const;

    std::vector<std::unique_ptr<ITool>> tools_;
};

} // namespace agent
