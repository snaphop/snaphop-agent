#include "agent/tool.hpp"
#include "agent/config.hpp"
#include <filesystem>
#include <algorithm>

namespace agent {

namespace fs = std::filesystem;

// WorkspacePath safety enforcement.
std::expected<fs::path, WorkspacePathError> resolve_workspace_path(const fs::path& workspace,
                                                                   std::string_view rel_path) {
    // Reject embedded NUL bytes.
    if (rel_path.find('\0') != std::string_view::npos)
        return std::unexpected(WorkspacePathError{"Path contains NUL byte"});

    std::string rel_str(rel_path);

    // Reject absolute paths.
    fs::path rel(rel_str);
    if (rel.is_absolute())
        return std::unexpected(WorkspacePathError{"Absolute paths are not allowed"});

    // Resolve the candidate path.
    std::error_code ec;
    fs::path canonical_ws = fs::weakly_canonical(workspace, ec);
    if (ec)
        return std::unexpected(WorkspacePathError{"Cannot resolve workspace: " + ec.message()});

    // Resolve existing parents too: the final file may not exist yet.
    fs::path candidate = fs::weakly_canonical(canonical_ws / rel, ec);
    if (ec)
        return std::unexpected(WorkspacePathError{"Cannot resolve path: " + ec.message()});
    auto relative = candidate.lexically_relative(canonical_ws);
    if (relative.empty() || *relative.begin() == "..")
        return std::unexpected(WorkspacePathError{"Path escapes workspace boundary"});

    return candidate;
}

// ToolRegistry

void ToolRegistry::register_tool(std::unique_ptr<ITool> tool) {
    auto name = tool->definition().name;
    auto it = std::find_if(tools_.begin(), tools_.end(),
                           [&](const auto& t) { return t->definition().name == name; });
    if (it != tools_.end()) {
        *it = std::move(tool);
    } else {
        tools_.push_back(std::move(tool));
    }
}

ITool* ToolRegistry::find(const std::string& name) const {
    for (const auto& t : tools_) {
        if (t->definition().name == name)
            return t.get();
    }
    return nullptr;
}

std::vector<ToolDefinition> ToolRegistry::definitions() const {
    std::vector<ToolDefinition> defs;
    defs.reserve(tools_.size());
    for (const auto& t : tools_)
        defs.push_back(t->definition());
    return defs;
}

std::expected<void, std::string> ToolRegistry::validate(const ToolCall& tc,
                                                        const ToolDefinition& def) const {
    if (!tc.arguments.is_object())
        return std::unexpected("Tool arguments must be a JSON object");

    for (const auto& param : def.params) {
        if (!tc.arguments.contains(param.name)) {
            if (param.required)
                return std::unexpected("Missing required parameter: " + param.name);
            continue;
        }

        const auto& val = tc.arguments[param.name];
        bool type_ok = false;
        if (param.type == "string")
            type_ok = val.is_string();
        else if (param.type == "boolean")
            type_ok = val.is_boolean();
        else if (param.type == "integer")
            type_ok = val.is_number_integer();
        else if (param.type == "number")
            type_ok = val.is_number();
        else if (param.type == "object")
            type_ok = val.is_object();
        else if (param.type == "array")
            type_ok = val.is_array();
        else
            type_ok = false;

        if (!type_ok)
            return std::unexpected("Parameter '" + param.name + "' has wrong type, expected " +
                                   param.type);
    }
    return {};
}

std::expected<ToolResult, std::string> ToolRegistry::execute(const ToolCall& tc,
                                                             const ToolContext& ctx) const {
    ITool* tool = find(tc.name);
    if (!tool)
        return std::unexpected("Unknown tool: " + tc.name);

    auto validation = validate(tc, tool->definition());
    if (!validation)
        return std::unexpected(validation.error());

    if (ctx.stop.stop_requested())
        return std::unexpected("Tool cancelled");
    try {
        return tool->call(tc, ctx);
    } catch (const std::exception&) {
        return std::unexpected("Tool execution failed");
    }
}

} // namespace agent
