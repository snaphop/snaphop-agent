#include <catch2/catch_test_macros.hpp>
#include "agent/tool.hpp"
#include "agent/config.hpp"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace agent {
std::unique_ptr<ITool> make_read_file_tool();
std::unique_ptr<ITool> make_write_file_tool();
} // namespace agent

// Helper: create a temp workspace.
static fs::path make_workspace() {
    auto ws = fs::temp_directory_path() / ("agent_ws_" + std::to_string(getpid()));
    fs::create_directories(ws);
    return ws;
}

// Simple mock tool for registry tests.
class PingTool : public agent::ITool {
  public:
    agent::ToolDefinition definition() const override {
        return {.name = "ping", .description = "pong", .params = {}, .dangerous = false};
    }
    agent::ToolResult call(const agent::ToolCall& tc, const agent::ToolContext&) override {
        return {.call_id = tc.call_id, .ok = true, .content = "pong", .error = ""};
    }
};

TEST_CASE("ToolRegistry: register and find tool", "[registry]") {
    agent::ToolRegistry reg;
    reg.register_tool(std::make_unique<PingTool>());

    CHECK(reg.find("ping") != nullptr);
    CHECK(reg.find("unknown") == nullptr);
    CHECK(reg.size() == 1);
}

TEST_CASE("ToolRegistry: rejects unknown tools", "[registry]") {
    agent::ToolRegistry reg;
    agent::ToolCall tc{.call_id = "c1", .name = "ghost", .arguments = agent::JsonValue::object()};
    agent::ToolContext ctx{.workspace = "/tmp", .limits = nullptr};

    auto result = reg.execute(tc, ctx);
    REQUIRE(!result.has_value());
    CHECK(result.error().find("Unknown tool") != std::string::npos);
}

TEST_CASE("ToolRegistry: validates required parameters", "[registry]") {
    agent::ToolRegistry reg;
    reg.register_tool(std::make_unique<PingTool>());

    agent::LimitsConfig limits;
    auto ws = make_workspace();
    agent::ToolContext ctx{.workspace = ws, .limits = &limits};
    agent::ToolCall tc{.call_id = "c1", .name = "ping", .arguments = agent::JsonValue::object()};
    auto result = reg.execute(tc, ctx);
    REQUIRE(result.has_value());
    CHECK(result->ok);

    fs::remove_all(ws);
}

TEST_CASE("WorkspacePath: rejects absolute paths", "[workspace]") {
    auto ws = make_workspace();
    auto result = agent::resolve_workspace_path(ws, "/etc/passwd");
    REQUIRE(!result.has_value());
    CHECK(result.error().message.find("Absolute") != std::string::npos);
    fs::remove_all(ws);
}

TEST_CASE("WorkspacePath: rejects path traversal", "[workspace]") {
    auto ws = make_workspace();
    auto result = agent::resolve_workspace_path(ws, "../secret");
    REQUIRE(!result.has_value());
    CHECK(result.error().message.find("escapes") != std::string::npos);
    fs::remove_all(ws);
}

TEST_CASE("WorkspacePath: rejects NUL bytes", "[workspace]") {
    auto ws = make_workspace();
    // Use explicit length so the embedded NUL is included.
    std::string evil("file\0name", 9);
    auto result = agent::resolve_workspace_path(ws, std::string_view(evil.data(), evil.size()));
    REQUIRE(!result.has_value());
    CHECK(result.error().message.find("NUL") != std::string::npos);
    fs::remove_all(ws);
}

TEST_CASE("WorkspacePath: allows normal relative paths", "[workspace]") {
    auto ws = make_workspace();
    auto result = agent::resolve_workspace_path(ws, "subdir/file.txt");
    REQUIRE(result.has_value());
    auto rel = result->lexically_relative(ws);
    CHECK(!rel.empty());
    CHECK(!rel.string().starts_with(".."));
    fs::remove_all(ws);
}

TEST_CASE("WorkspacePath: normalizes dot-dot that stays inside", "[workspace]") {
    auto ws = make_workspace();
    auto result = agent::resolve_workspace_path(ws, "subdir/../file.txt");
    REQUIRE(result.has_value());
    fs::remove_all(ws);
}

TEST_CASE("ReadFileTool: reads within workspace", "[tools]") {
    auto ws = make_workspace();
    std::ofstream f(ws / "hello.txt");
    f << "hello world\n";
    f.close();

    agent::ToolRegistry reg;
    reg.register_tool(agent::make_read_file_tool());

    agent::LimitsConfig limits;
    agent::ToolContext ctx{.workspace = ws, .limits = &limits};
    agent::ToolCall tc{
        .call_id = "c1", .name = "read_file", .arguments = agent::JsonValue{{"path", "hello.txt"}}};
    auto result = reg.execute(tc, ctx);
    REQUIRE(result.has_value());
    CHECK(result->ok);
    CHECK(result->content["content"].get<std::string>() == "hello world\n");

    fs::remove_all(ws);
}

TEST_CASE("ReadFileTool: rejects path escaping workspace", "[tools]") {
    auto ws = make_workspace();

    agent::ToolRegistry reg;
    reg.register_tool(agent::make_read_file_tool());

    agent::LimitsConfig limits;
    agent::ToolContext ctx{.workspace = ws, .limits = &limits};
    agent::ToolCall tc{.call_id = "c1",
                       .name = "read_file",
                       .arguments = agent::JsonValue{{"path", "../../etc/passwd"}}};
    auto result = reg.execute(tc, ctx);
    REQUIRE(result.has_value());
    CHECK(!result->ok);
    CHECK(result->error.find("escapes") != std::string::npos);

    fs::remove_all(ws);
}

TEST_CASE("WriteFileTool: writes inside workspace", "[tools]") {
    auto ws = make_workspace();

    agent::ToolRegistry reg;
    reg.register_tool(agent::make_write_file_tool());

    agent::LimitsConfig limits;
    agent::ToolContext ctx{.workspace = ws, .limits = &limits};
    agent::ToolCall tc{.call_id = "c1",
                       .name = "write_file",
                       .arguments = agent::JsonValue{{"path", "output.txt"}, {"content", "data"}}};
    auto result = reg.execute(tc, ctx);
    REQUIRE(result.has_value());
    CHECK(result->ok);
    CHECK(fs::exists(ws / "output.txt"));

    fs::remove_all(ws);
}

TEST_CASE("WorkspacePath: new files cannot escape through symlink parents", "[workspace]") {
    auto ws = make_workspace();
    auto outside = ws.string() + "_outside";
    fs::create_directories(outside);
    fs::create_directory_symlink(outside, ws / "link");
    CHECK_FALSE(agent::resolve_workspace_path(ws, "link/new.txt"));
    CHECK_FALSE(agent::resolve_workspace_path(ws, "link/../escape.txt"));
    CHECK(agent::resolve_workspace_path(ws, "..valid.txt"));
    fs::remove_all(ws);
    fs::remove_all(outside);
}

TEST_CASE("ToolRegistry: optional arguments are type checked", "[registry]") {
    auto ws = make_workspace();
    agent::ToolRegistry registry;
    registry.register_tool(agent::make_write_file_tool());
    agent::ToolContext ctx{.workspace = ws};
    auto result = registry.execute(
        {"c", "write_file", {{"path", "file"}, {"content", "text"}, {"overwrite", "yes"}}}, ctx);
    CHECK_FALSE(result);
    CHECK_FALSE(fs::exists(ws / "file"));
    fs::remove_all(ws);
}

TEST_CASE("WriteFileTool: model cannot grant overwrite policy", "[tools]") {
    auto ws = make_workspace();
    std::ofstream(ws / "file") << "original";
    agent::ToolRegistry registry;
    registry.register_tool(agent::make_write_file_tool());
    agent::ToolContext ctx{.workspace = ws};
    agent::ToolCall call{
        "c", "write_file", {{"path", "file"}, {"content", "replacement"}, {"overwrite", true}}};
    auto denied = registry.execute(call, ctx);
    REQUIRE(denied);
    CHECK_FALSE(denied->ok);
    ctx.allow_overwrite = true;
    auto approved = registry.execute(call, ctx);
    REQUIRE(approved);
    CHECK(approved->ok);
    fs::remove_all(ws);
}

TEST_CASE("ReadFileTool: serialized pages reconstruct escaped UTF-8 without gaps", "[tools]") {
    const auto ws = make_workspace();
    std::string text;
    for (int i = 0; i < 80; ++i)
        text += "世界\"\\\n";
    std::ofstream(ws / "pages.txt", std::ios::binary) << text;
    agent::ToolRegistry reg;
    reg.register_tool(agent::make_read_file_tool());
    agent::LimitsConfig limits;
    limits.max_tool_result_bytes = 180;
    agent::ToolContext ctx{.workspace = ws, .limits = &limits};
    uint64_t offset = 0;
    std::string reconstructed;
    bool finished = false;
    for (size_t page = 0; page < text.size() + 1; ++page) {
        agent::ToolCall call{
            .call_id = "page",
            .name = "read_file",
            .arguments = {{"path", "pages.txt"}, {"offset", offset}, {"limit", 100}}};
        const auto result = reg.execute(call, ctx);
        REQUIRE(result.has_value());
        REQUIRE(result->ok);
        CHECK(agent::to_json_string(result->content).size() <= limits.max_tool_result_bytes);
        const auto chunk = result->content["content"].get<std::string>();
        reconstructed += chunk;
        const auto next = result->content["next_offset"].get<uint64_t>();
        CHECK(next == offset + chunk.size());
        if (!result->content["truncated"].get<bool>()) {
            finished = true;
            break;
        }
        REQUIRE(next > offset);
        offset = next;
    }
    CHECK(finished);
    CHECK(reconstructed == text);
    fs::remove_all(ws);
}

TEST_CASE("ReadFileTool: invalid offsets limits and non-text pages fail safely", "[tools]") {
    const auto ws = make_workspace();
    std::ofstream(ws / "utf8.txt", std::ios::binary) << "世界";
    std::ofstream(ws / "invalid.txt", std::ios::binary) << std::string(1, '\xff');
    agent::ToolRegistry reg;
    reg.register_tool(agent::make_read_file_tool());
    agent::LimitsConfig limits;
    agent::ToolContext ctx{.workspace = ws, .limits = &limits};
    for (const auto& args : {agent::JsonValue{{"path", "utf8.txt"}, {"offset", -1}},
                             agent::JsonValue{{"path", "utf8.txt"}, {"offset", 1}},
                             agent::JsonValue{{"path", "utf8.txt"}, {"limit", 0}},
                             agent::JsonValue{{"path", "utf8.txt"}, {"limit", -1}},
                             agent::JsonValue{{"path", "utf8.txt"}, {"limit", 1}},
                             agent::JsonValue{{"path", "invalid.txt"}}}) {
        const auto result = reg.execute({"bad", "read_file", args}, ctx);
        CHECK((!result.has_value() || !result->ok));
    }
    limits.max_tool_result_bytes = 8;
    const auto tiny = reg.execute({"tiny", "read_file", {{"path", "utf8.txt"}}}, ctx);
    REQUIRE(tiny.has_value());
    CHECK_FALSE(tiny->ok);
    fs::remove_all(ws);
}

TEST_CASE("Tool registry caches definitions and validates canonical schema constraints",
          "[registry]") {
    class SchemaTool final : public agent::ITool {
      public:
        int& definitions;
        int& executions;
        SchemaTool(int& defs, int& calls) : definitions(defs), executions(calls) {}
        agent::ToolDefinition definition() const override {
            ++definitions;
            return {.name = "typed",
                    .description = "Typed",
                    .params = {{.name = "args",
                                .type = "array",
                                .required = true,
                                .description = "Strings",
                                .item_type = "string"},
                               {.name = "count",
                                .type = "integer",
                                .required = true,
                                .description = "Bounded",
                                .minimum = 1,
                                .maximum = 3}}};
        }
        agent::ToolResult call(const agent::ToolCall&, const agent::ToolContext&) override {
            ++executions;
            return {"wrong-provider-id", true, "ok", ""};
        }
    };
    int definitions = 0, executions = 0;
    agent::ToolRegistry registry;
    registry.register_tool(std::make_unique<SchemaTool>(definitions, executions));
    registry.freeze();
    for (int i = 0; i < 10; ++i) {
        REQUIRE(registry.find("typed"));
        CHECK(registry.definitions().size() == 1);
        const auto prepared = registry.prepare({"id", "typed", {{"args", {"one"}}, {"count", 2}}});
        REQUIRE(prepared.has_value());
        const auto result = registry.execute(*prepared, {});
        REQUIRE(result.has_value());
        CHECK(result->call_id == "id");
    }
    CHECK(definitions == 1);
    CHECK(executions == 10);
    for (const auto& arguments :
         {agent::JsonValue{{"args", {1}}, {"count", 2}},
          agent::JsonValue{{"args", {"one"}}, {"count", 0}},
          agent::JsonValue{{"args", {"one"}}, {"count", 4}},
          agent::JsonValue{{"args", {"one"}}, {"count", 2}, {"extra", true}}})
        CHECK_FALSE(registry.prepare({"bad", "typed", arguments}));
    CHECK(executions == 10);
    const auto schema = agent::tool_input_schema(*registry.definition("typed"));
    CHECK(schema["additionalProperties"] == false);
    CHECK(schema["properties"]["args"]["items"]["type"] == "string");
    CHECK(schema["properties"]["count"]["minimum"] == 1);
    CHECK(schema["properties"]["count"]["maximum"] == 3);
    REQUIRE_THROWS_AS(registry.register_tool(agent::make_read_file_tool()), std::logic_error);
}
