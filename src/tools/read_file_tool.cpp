#include "agent/tool.hpp"
#include "agent/config.hpp"
#include <fstream>
#include <sstream>

namespace agent {

class ReadFileTool : public ITool {
public:
    ToolDefinition definition() const override {
        return {
            .name = "read_file",
            .description = "Read a workspace-relative UTF-8 text file.",
            .params = {
                {.name = "path", .type = "string", .required = true,
                 .description = "Workspace-relative file path"},
            },
            .dangerous = false,
        };
    }

    ToolResult call(const ToolCall& tc, const ToolContext& ctx) override {
        std::string rel = tc.arguments.value("path", "");
        auto resolved = resolve_workspace_path(ctx.workspace, rel);
        if (!resolved) {
            return {.call_id = tc.call_id, .ok = false,
                    .content = nullptr, .error = resolved.error().message};
        }

        std::ifstream f(*resolved, std::ios::binary);
        if (!f) {
            return {.call_id = tc.call_id, .ok = false,
                    .content = nullptr, .error = "Cannot open file: " + rel};
        }

        size_t max_bytes = ctx.limits ? ctx.limits->max_tool_result_bytes : 65536u;
        std::string buf(max_bytes + 1, '\0');
        f.read(buf.data(), static_cast<std::streamsize>(max_bytes + 1));
        auto n = f.gcount();
        bool truncated = (n > static_cast<std::streamsize>(max_bytes));
        buf.resize(std::min(static_cast<size_t>(n), max_bytes));

        JsonValue result;
        result["content"]   = buf;
        result["truncated"] = truncated;
        result["path"]      = rel;
        return {.call_id = tc.call_id, .ok = true, .content = result, .error = ""};
    }
};

std::unique_ptr<ITool> make_read_file_tool() {
    return std::make_unique<ReadFileTool>();
}

} // namespace agent
