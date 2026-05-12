#include "agent/tool.hpp"
#include <filesystem>

namespace agent {

namespace fs = std::filesystem;

class ListDirTool : public ITool {
public:
    ToolDefinition definition() const override {
        return {
            .name = "list_dir",
            .description = "List entries in a workspace-relative directory.",
            .params = {
                {.name = "path", .type = "string", .required = false,
                 .description = "Workspace-relative directory path (default: '.')"}
            },
            .dangerous = false,
        };
    }

    ToolResult call(const ToolCall& tc, const ToolContext& ctx) override {
        std::string rel = tc.arguments.value("path", ".");

        auto resolved = resolve_workspace_path(ctx.workspace, rel);
        if (!resolved) {
            return {.call_id = tc.call_id, .ok = false,
                    .content = nullptr, .error = resolved.error().message};
        }

        std::error_code ec;
        if (!fs::is_directory(*resolved, ec)) {
            return {.call_id = tc.call_id, .ok = false,
                    .content = nullptr, .error = "Not a directory: " + rel};
        }

        JsonValue entries = JsonValue::array();
        for (const auto& entry : fs::directory_iterator(*resolved, ec)) {
            if (ec) break;
            JsonValue item;
            item["name"] = entry.path().filename().string();
            item["type"] = entry.is_directory(ec) ? "dir" : "file";
            item["size"] = entry.is_regular_file(ec) ? static_cast<int64_t>(entry.file_size(ec)) : 0;
            entries.push_back(std::move(item));
        }

        return {.call_id = tc.call_id, .ok = true, .content = entries, .error = ""};
    }
};

std::unique_ptr<ITool> make_list_dir_tool() {
    return std::make_unique<ListDirTool>();
}

} // namespace agent
