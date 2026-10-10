#pragma once

#include "json.hpp"
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
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
    std::string item_type = {}; // Array element type; empty means unconstrained.
    std::optional<int64_t> minimum = {};
    std::optional<int64_t> maximum = {};
};

struct ToolDefinition {
    std::string name;
    std::string description;
    std::vector<ToolParam> params;
    bool dangerous = false;
    // Opt-in contract: call() is thread-safe and has no filesystem/process mutations.
    bool read_only = false;
};

// One canonical input schema for provider projection and local validation.
JsonValue tool_input_schema(const ToolDefinition& definition);

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

class ToolRegistry;

// Immutable validated arguments. Registry/tool ownership must outlive this value.
class PreparedToolCall {
  public:
    const ToolCall& call() const {
        return call_;
    }
    bool read_only() const {
        return read_only_;
    }

  private:
    friend class ToolRegistry;
    PreparedToolCall(const ToolRegistry* owner, ITool* tool, ToolCall call, bool read_only)
        : owner_(owner), tool_(tool), call_(std::move(call)), read_only_(read_only) {}
    const ToolRegistry* owner_;
    ITool* tool_; // Non-owning; registration is frozen before preparation.
    ToolCall call_;
    bool read_only_;
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

    const ToolDefinition* definition(std::string_view name) const;
    // Freeze registration before sharing prepared calls with workers.
    void freeze() {
        frozen_ = true;
    }
    std::expected<PreparedToolCall, std::string> prepare(ToolCall call) const;
    std::expected<ToolResult, std::string> execute(const PreparedToolCall& call,
                                                   const ToolContext& ctx) const;

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

    struct Entry {
        std::unique_ptr<ITool> tool;
        ToolDefinition definition;
    };
    std::vector<Entry> tools_;
    bool frozen_ = false;
};

} // namespace agent
