#include "agent/tool.hpp"
#include "agent/config.hpp"
#include <fstream>
#include <filesystem>

namespace agent {

namespace fs = std::filesystem;

class WriteFileTool : public ITool {
  public:
    ToolDefinition definition() const override {
        return {
            .name = "write_file",
            .description = "Write content to a workspace-relative file.",
            .params =
                {
                    {.name = "path",
                     .type = "string",
                     .required = true,
                     .description = "Workspace-relative file path"},
                    {.name = "content",
                     .type = "string",
                     .required = true,
                     .description = "UTF-8 text content to write"},
                    {.name = "overwrite",
                     .type = "boolean",
                     .required = false,
                     .description = "Allow overwriting an existing file (default: false)"},
                },
            .dangerous = false,
        };
    }

    ToolResult call(const ToolCall& tc, const ToolContext& ctx) override {
        std::string rel = tc.arguments.value("path", "");
        std::string content = tc.arguments.value("content", "");
        bool overwrite = tc.arguments.value("overwrite", false);

        auto resolved = resolve_workspace_path(ctx.workspace, rel);
        if (!resolved) {
            return {.call_id = tc.call_id,
                    .ok = false,
                    .content = nullptr,
                    .error = resolved.error().message};
        }

        std::error_code ec;
        if (fs::exists(*resolved, ec) && (!overwrite || !ctx.allow_overwrite)) {
            return {.call_id = tc.call_id,
                    .ok = false,
                    .content = nullptr,
                    .error = "File already exists. Overwrite requires overwrite=true and "
                             "tools.allow_overwrite policy: " +
                             rel};
        }

        // Create parent directories if needed.
        fs::create_directories(resolved->parent_path(), ec);
        if (ec) {
            return {.call_id = tc.call_id,
                    .ok = false,
                    .content = nullptr,
                    .error = "Cannot create directories: " + ec.message()};
        }

        std::ofstream f(*resolved, std::ios::binary | std::ios::trunc);
        if (!f) {
            return {.call_id = tc.call_id,
                    .ok = false,
                    .content = nullptr,
                    .error = "Cannot open file for writing: " + rel};
        }

        f << content;
        if (!f) {
            return {.call_id = tc.call_id,
                    .ok = false,
                    .content = nullptr,
                    .error = "Write failed: " + rel};
        }

        JsonValue result;
        result["path"] = rel;
        result["bytes_written"] = static_cast<int64_t>(content.size());
        return {.call_id = tc.call_id, .ok = true, .content = result, .error = ""};
    }
};

std::unique_ptr<ITool> make_write_file_tool() {
    return std::make_unique<WriteFileTool>();
}

} // namespace agent
