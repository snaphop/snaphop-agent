#include <catch2/catch_test_macros.hpp>
#include "agent/tool.hpp"
#include "agent/config.hpp"
#include <filesystem>

namespace fs = std::filesystem;

namespace agent { std::unique_ptr<ITool> make_shell_tool(); }

static fs::path make_workspace() {
    auto ws = fs::temp_directory_path() / ("shell_ws_" + std::to_string(getpid()));
    fs::create_directories(ws);
    return ws;
}

TEST_CASE("ShellTool: runs a simple command", "[shell]") {
    auto ws = make_workspace();
    agent::LimitsConfig limits;
    limits.max_tool_seconds    = 5;
    limits.max_shell_output_bytes = 4096;

    auto shell = agent::make_shell_tool();
    agent::ToolContext ctx{.workspace = ws, .limits = &limits};
    agent::ToolCall tc{
        .call_id = "c1",
        .name = "shell",
        .arguments = agent::JsonValue{{"command", "echo"}, {"args", agent::JsonValue::array({"hello"})}}
    };

    auto result = shell->call(tc, ctx);
    CHECK(result.ok);
    CHECK(result.content["output"].get<std::string>().find("hello") != std::string::npos);
    CHECK(result.content["exit_code"].get<int>() == 0);

    fs::remove_all(ws);
}

TEST_CASE("ShellTool: times out and returns timed_out=true", "[shell]") {
    auto ws = make_workspace();
    agent::LimitsConfig limits;
    limits.max_tool_seconds    = 1;
    limits.max_shell_output_bytes = 4096;

    auto shell = agent::make_shell_tool();
    agent::ToolContext ctx{.workspace = ws, .limits = &limits};
    agent::ToolCall tc{
        .call_id = "c1",
        .name = "shell",
        .arguments = agent::JsonValue{{"command", "sleep"}, {"args", agent::JsonValue::array({"10"})}}
    };

    auto result = shell->call(tc, ctx);
    CHECK(!result.ok);
    CHECK(result.content["timed_out"].get<bool>() == true);

    fs::remove_all(ws);
}

TEST_CASE("ShellTool: truncates output when exceeding limit", "[shell]") {
    auto ws = make_workspace();
    agent::LimitsConfig limits;
    limits.max_tool_seconds    = 10;
    limits.max_shell_output_bytes = 64;

    auto shell = agent::make_shell_tool();
    agent::ToolContext ctx{.workspace = ws, .limits = &limits};
    agent::ToolCall tc{
        .call_id = "c1",
        .name = "shell",
        .arguments = agent::JsonValue{
            {"command", "python3"},
            {"args", agent::JsonValue::array({"-c", "print('x' * 1000)"})}
        }
    };

    auto result = shell->call(tc, ctx);
    // Output should be truncated.
    CHECK(result.content["truncated"].get<bool>() == true);
    CHECK(result.content["output"].get<std::string>().size() <= 64);

    fs::remove_all(ws);
}
