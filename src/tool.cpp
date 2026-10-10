#include "agent/tool.hpp"
#include "agent/config.hpp"
#include <filesystem>
#include <algorithm>
#include <stdexcept>

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

JsonValue tool_input_schema(const ToolDefinition& def) {
    JsonValue properties = JsonValue::object();
    JsonValue required = JsonValue::array();
    for (const auto& param : def.params) {
        JsonValue schema{{"type", param.type}, {"description", param.description}};
        if (param.type == "array" && !param.item_type.empty())
            schema["items"] = {{"type", param.item_type}};
        if (param.minimum)
            schema["minimum"] = *param.minimum;
        if (param.maximum)
            schema["maximum"] = *param.maximum;
        properties[param.name] = std::move(schema);
        if (param.required)
            required.push_back(param.name);
    }
    return {{"type", "object"},
            {"properties", std::move(properties)},
            {"required", std::move(required)},
            {"additionalProperties", false}};
}

void ToolRegistry::register_tool(std::unique_ptr<ITool> tool) {
    if (frozen_)
        throw std::logic_error("Tool registry is frozen");
    if (!tool)
        throw std::invalid_argument("Cannot register a null tool");
    auto def = tool->definition();
    auto it = std::find_if(tools_.begin(), tools_.end(),
                           [&](const auto& entry) { return entry.definition.name == def.name; });
    Entry entry{std::move(tool), std::move(def)};
    if (it != tools_.end())
        *it = std::move(entry);
    else
        tools_.push_back(std::move(entry));
}

ITool* ToolRegistry::find(const std::string& name) const {
    for (const auto& entry : tools_) {
        if (entry.definition.name == name)
            return entry.tool.get();
    }
    return nullptr;
}

const ToolDefinition* ToolRegistry::definition(std::string_view name) const {
    for (const auto& entry : tools_) {
        if (entry.definition.name == name)
            return &entry.definition;
    }
    return nullptr;
}

std::vector<ToolDefinition> ToolRegistry::definitions() const {
    std::vector<ToolDefinition> defs;
    defs.reserve(tools_.size());
    for (const auto& entry : tools_)
        defs.push_back(entry.definition);
    return defs;
}

static bool matches_type(const JsonValue& value, std::string_view type) {
    if (type == "string")
        return value.is_string();
    if (type == "boolean")
        return value.is_boolean();
    if (type == "integer")
        return value.is_number_integer();
    if (type == "number")
        return value.is_number();
    if (type == "object")
        return value.is_object();
    if (type == "array")
        return value.is_array();
    return false;
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
        const bool type_ok = matches_type(val, param.type);
        if (!type_ok)
            return std::unexpected("Parameter '" + param.name + "' has wrong type, expected " +
                                   param.type);
        if (param.type == "array" && !param.item_type.empty()) {
            for (const auto& item : val) {
                if (!matches_type(item, param.item_type))
                    return std::unexpected("Parameter '" + param.name +
                                           "' has invalid array elements");
            }
        }
        if (val.is_number() && ((param.minimum && val.get<long double>() < *param.minimum) ||
                                (param.maximum && val.get<long double>() > *param.maximum)))
            return std::unexpected("Parameter '" + param.name + "' is out of range");
    }
    for (const auto& [name, value] : tc.arguments.items()) {
        (void)value;
        if (std::none_of(def.params.begin(), def.params.end(),
                         [&](const auto& param) { return param.name == name; }))
            return std::unexpected("Unknown parameter: " + name);
    }
    return {};
}

std::expected<PreparedToolCall, std::string> ToolRegistry::prepare(ToolCall call) const {
    if (!frozen_)
        return std::unexpected("Tool registry must be frozen before preparation");
    const auto* def = definition(call.name);
    if (!def)
        return std::unexpected("Unknown tool: " + call.name);
    auto valid = validate(call, *def);
    if (!valid)
        return std::unexpected(valid.error());
    auto* tool = find(call.name);
    return PreparedToolCall(this, tool, std::move(call), def->read_only && !def->dangerous);
}

std::expected<ToolResult, std::string> ToolRegistry::execute(const PreparedToolCall& call,
                                                             const ToolContext& ctx) const {
    if (call.owner_ != this)
        return std::unexpected("Prepared call belongs to another registry");
    if (ctx.stop.stop_requested())
        return std::unexpected("Tool cancelled");
    try {
        auto result = call.tool_->call(call.call_, ctx);
        result.call_id = call.call_.call_id;
        return result;
    } catch (const std::exception&) {
        return std::unexpected("Tool execution failed");
    }
}

std::expected<ToolResult, std::string> ToolRegistry::execute(const ToolCall& tc,
                                                             const ToolContext& ctx) const {
    const auto* def = definition(tc.name);
    if (!def)
        return std::unexpected("Unknown tool: " + tc.name);
    auto valid = validate(tc, *def);
    if (!valid)
        return std::unexpected(valid.error());
    if (ctx.stop.stop_requested())
        return std::unexpected("Tool cancelled");
    try {
        auto result = find(tc.name)->call(tc, ctx);
        result.call_id = tc.call_id;
        return result;
    } catch (const std::exception&) {
        return std::unexpected("Tool execution failed");
    }
}

} // namespace agent
