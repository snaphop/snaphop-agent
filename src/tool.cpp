#include "agent/tool.hpp"
#include "agent/config.hpp"
#include <filesystem>
#include <algorithm>

namespace agent {

namespace fs = std::filesystem;

// WorkspacePath safety enforcement.
std::expected<fs::path, WorkspacePathError>
resolve_workspace_path(const fs::path& workspace, std::string_view rel_path) {
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
    if (ec) return std::unexpected(WorkspacePathError{"Cannot resolve workspace: " + ec.message()});

    fs::path candidate = canonical_ws / rel;

    // Normalize by resolving . and .. lexically first.
    candidate = candidate.lexically_normal();

    // Check containment: the candidate must start with the workspace prefix.
    auto rel_check = candidate.lexically_relative(canonical_ws);
    if (rel_check.empty() || rel_check.string().starts_with(".."))
        return std::unexpected(WorkspacePathError{"Path escapes workspace boundary"});

    // If the path exists, resolve symlinks and re-check.
    if (fs::exists(candidate, ec)) {
        fs::path real = fs::canonical(candidate, ec);
        if (!ec) {
            fs::path ws_real = fs::canonical(canonical_ws, ec);
            if (!ec) {
                auto rel2 = real.lexically_relative(ws_real);
                if (rel2.empty() || rel2.string().starts_with(".."))
                    return std::unexpected(WorkspacePathError{"Symlink escapes workspace boundary"});
                return real;
            }
        }
    }
    return candidate;
}

// ToolRegistry

void ToolRegistry::register_tool(std::unique_ptr<ITool> tool) {
    auto name = tool->definition().name;
    auto it = std::find_if(tools_.begin(), tools_.end(),
        [&](const auto& t){ return t->definition().name == name; });
    if (it != tools_.end()) {
        *it = std::move(tool);
    } else {
        tools_.push_back(std::move(tool));
    }
}

ITool* ToolRegistry::find(const std::string& name) const {
    for (const auto& t : tools_) {
        if (t->definition().name == name) return t.get();
    }
    return nullptr;
}

std::vector<ToolDefinition> ToolRegistry::definitions() const {
    std::vector<ToolDefinition> defs;
    defs.reserve(tools_.size());
    for (const auto& t : tools_) defs.push_back(t->definition());
    return defs;
}

std::expected<void, std::string>
ToolRegistry::validate(const ToolCall& tc, const ToolDefinition& def) const {
    if (!tc.arguments.is_object())
        return std::unexpected("Tool arguments must be a JSON object");

    for (const auto& param : def.params) {
        if (!param.required) continue;
        if (!tc.arguments.contains(param.name))
            return std::unexpected("Missing required parameter: " + param.name);

        const auto& val = tc.arguments[param.name];
        bool type_ok = false;
        if      (param.type == "string")  type_ok = val.is_string();
        else if (param.type == "boolean") type_ok = val.is_boolean();
        else if (param.type == "integer") type_ok = val.is_number_integer();
        else if (param.type == "number")  type_ok = val.is_number();
        else if (param.type == "object")  type_ok = val.is_object();
        else if (param.type == "array")   type_ok = val.is_array();
        else                              type_ok = true; // unknown type: pass through

        if (!type_ok)
            return std::unexpected("Parameter '" + param.name + "' has wrong type, expected " + param.type);
    }
    return {};
}

std::expected<ToolResult, std::string>
ToolRegistry::execute(const ToolCall& tc, const ToolContext& ctx) const {
    ITool* tool = find(tc.name);
    if (!tool) return std::unexpected("Unknown tool: " + tc.name);

    auto validation = validate(tc, tool->definition());
    if (!validation) return std::unexpected(validation.error());

    return tool->call(tc, ctx);
}

} // namespace agent
