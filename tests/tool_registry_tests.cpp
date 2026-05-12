#include <catch2/catch_test_macros.hpp>
#include "agent/tool.hpp"
#include "agent/config.hpp"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace agent {
std::unique_ptr<ITool> make_read_file_tool();
std::unique_ptr<ITool> make_write_file_tool();
}

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
    agent::ToolCall tc{.call_id = "c1", .name = "read_file",
                       .arguments = agent::JsonValue{{"path", "hello.txt"}}};
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
    agent::ToolCall tc{.call_id = "c1", .name = "read_file",
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
    agent::ToolCall tc{.call_id = "c1", .name = "write_file",
                       .arguments = agent::JsonValue{{"path", "output.txt"}, {"content", "data"}}};
    auto result = reg.execute(tc, ctx);
    REQUIRE(result.has_value());
    CHECK(result->ok);
    CHECK(fs::exists(ws / "output.txt"));

    fs::remove_all(ws);
}
