#include "agent/tool.hpp"
#include "agent/config.hpp"
#include "agent/tool_result_budget.hpp"
#include <fstream>
#include <limits>

namespace agent {

class ReadFileTool : public ITool {
  public:
    ToolDefinition definition() const override {
        return {
            .name = "read_file",
            .description = "Read a bounded page of a workspace-relative UTF-8 text file. "
                           "Use next_offset as offset to continue when truncated.",
            .params =
                {
                    {.name = "path",
                     .type = "string",
                     .required = true,
                     .description = "Workspace-relative file path"},
                    {.name = "offset",
                     .type = "integer",
                     .required = false,
                     .description = "Nonnegative byte offset at a UTF-8 boundary (default: 0)",
                     .minimum = 0},
                    {.name = "limit",
                     .type = "integer",
                     .required = false,
                     .description = "Positive maximum source bytes; capped by result budget",
                     .minimum = 1},
                },
            .dangerous = false,
            .read_only = true,
        };
    }

    ToolResult call(const ToolCall& tc, const ToolContext& ctx) override {
        std::string rel = tc.arguments.value("path", "");
        auto resolved = resolve_workspace_path(ctx.workspace, rel);
        if (!resolved) {
            return {.call_id = tc.call_id,
                    .ok = false,
                    .content = nullptr,
                    .error = resolved.error().message};
        }

        std::ifstream f(*resolved, std::ios::binary);
        if (!f) {
            return {.call_id = tc.call_id,
                    .ok = false,
                    .content = nullptr,
                    .error = "Cannot open file: " + rel};
        }

        auto fail = [&](std::string error) -> ToolResult {
            return {
                .call_id = tc.call_id, .ok = false, .content = nullptr, .error = std::move(error)};
        };
        auto unsigned_param = [&](const char* name, uint64_t fallback) -> std::optional<uint64_t> {
            if (!tc.arguments.contains(name))
                return fallback;
            const auto& value = tc.arguments[name];
            if (!value.is_number_integer() ||
                (!value.is_number_unsigned() && value.get<int64_t>() < 0))
                return std::nullopt;
            return value.get<uint64_t>();
        };
        const size_t max_bytes = ctx.limits ? ctx.limits->max_tool_result_bytes : 65536u;
        const auto offset = unsigned_param("offset", 0);
        const auto limit = unsigned_param("limit", max_bytes);
        if (!offset || *offset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()))
            return fail("offset must be a nonnegative representable byte offset");
        if (!limit || *limit == 0)
            return fail("limit must be a positive byte count");
        if (ctx.stop.stop_requested())
            return fail("Read cancelled");
        f.seekg(static_cast<std::streamoff>(*offset));
        if (!f)
            return fail("Cannot seek file: " + rel);
        const size_t page_bytes = static_cast<size_t>(std::min<uint64_t>(*limit, max_bytes));
        if (page_bytes >= static_cast<size_t>(std::numeric_limits<std::streamsize>::max()))
            return fail("Read limit is too large");
        std::string buf(page_bytes + 1, '\0');
        f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        if (f.bad())
            return fail("Cannot read file: " + rel);
        buf.resize(static_cast<size_t>(f.gcount()));
        if (!buf.empty() && (static_cast<unsigned char>(buf.front()) & 0xc0) == 0x80)
            return fail("offset must be at a UTF-8 code point boundary");
        // The extra byte lets prefix cuts avoid splitting a code point at the page edge.
        auto project = [&](size_t bytes) {
            std::string text = utf8_prefix(buf, std::min(bytes, page_bytes));
            const auto next = *offset + text.size();
            return JsonValue{{"content", std::move(text)},
                             {"truncated", next - *offset < buf.size()},
                             {"path", rel},
                             {"offset", *offset},
                             {"next_offset", next}};
        };
        JsonValue result;
        size_t low = 0, high = std::min(page_bytes, buf.size());
        try {
            result = project(0);
            if (to_json_string(result).size() > max_bytes)
                return fail("Result budget cannot fit read metadata");
            while (low < high) {
                const size_t mid = low + (high - low + 1) / 2;
                auto candidate = project(mid);
                if (to_json_string(candidate).size() <= max_bytes)
                    low = mid;
                else
                    high = mid - 1;
            }
            result = project(low);
            // Validate UTF-8 through the same serializer used for model input and persistence.
            (void)to_json_string(result);
        } catch (const JsonValue::type_error&) {
            return fail("File page is not valid UTF-8 text");
        }
        if (!buf.empty() && result["content"].get_ref<const std::string&>().empty())
            return fail("Read limit or result budget cannot fit one UTF-8 code point");
        if (ctx.stop.stop_requested())
            return fail("Read cancelled");
        return {.call_id = tc.call_id, .ok = true, .content = result, .error = ""};
    }
};

std::unique_ptr<ITool> make_read_file_tool() {
    return std::make_unique<ReadFileTool>();
}

} // namespace agent
