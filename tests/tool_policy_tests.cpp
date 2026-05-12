#include <catch2/catch_test_macros.hpp>
#include "agent/tool_policy.hpp"

static agent::ToolDefinition safe_tool(std::string name) {
    return {.name = std::move(name), .description = "test", .params = {}, .dangerous = false};
}

static agent::ToolDefinition dangerous_tool(std::string name) {
    return {.name = std::move(name), .description = "test", .params = {}, .dangerous = true};
}

TEST_CASE("ToolPolicy: allowed tool passes", "[policy]") {
    agent::ToolsConfig cfg;
    cfg.allow = {"echo", "list_dir"};
    cfg.deny  = {};
    agent::ToolPolicy policy(cfg);

    CHECK(policy.is_allowed(safe_tool("echo")));
    CHECK(policy.is_allowed(safe_tool("list_dir")));
}

TEST_CASE("ToolPolicy: explicit deny wins over allow", "[policy]") {
    agent::ToolsConfig cfg;
    cfg.allow = {"echo", "shell"};
    cfg.deny  = {"shell"};
    agent::ToolPolicy policy(cfg);

    CHECK(policy.is_allowed(safe_tool("echo")));
    CHECK(!policy.is_allowed(dangerous_tool("shell")));

    auto result = policy.check(dangerous_tool("shell"));
    REQUIRE(!result.has_value());
    CHECK(result.error().reason.find("explicitly denied") != std::string::npos);
}

TEST_CASE("ToolPolicy: unknown tool is denied", "[policy]") {
    agent::ToolsConfig cfg;
    cfg.allow = {"echo"};
    cfg.deny  = {};
    agent::ToolPolicy policy(cfg);

    CHECK(!policy.is_allowed(safe_tool("unknown_tool")));
    auto result = policy.check(safe_tool("unknown_tool"));
    REQUIRE(!result.has_value());
    CHECK(result.error().reason.find("not on the allow list") != std::string::npos);
}

TEST_CASE("ToolPolicy: dangerous tool blocked without enable_dangerous flag", "[policy]") {
    agent::ToolsConfig cfg;
    cfg.allow = {"shell"};
    cfg.deny  = {};
    cfg.enable_dangerous = false;
    agent::ToolPolicy policy(cfg);

    CHECK(!policy.is_allowed(dangerous_tool("shell")));
}

TEST_CASE("ToolPolicy: dangerous tool allowed with enable_dangerous flag", "[policy]") {
    agent::ToolsConfig cfg;
    cfg.allow = {"shell"};
    cfg.deny  = {};
    cfg.enable_dangerous = true;
    agent::ToolPolicy policy(cfg);

    CHECK(policy.is_allowed(dangerous_tool("shell")));
}
