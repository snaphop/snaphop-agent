#include <catch2/catch_test_macros.hpp>
#include "agent/config.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>

TEST_CASE("Config: rejects invalid limits", "[config]") {
    for (const auto* value : {"-1", "0", "1.5", "4294967296", "\"100\"", "null"}) {
        auto result =
            agent::parse_config(std::string("{\"limits\":{\"max_turn_seconds\":") + value + "}}");
        CHECK_FALSE(result.has_value());
    }
    CHECK(agent::parse_config(R"({"limits":{"max_turn_seconds":1}})"));
    CHECK(agent::parse_config("{}"));
}

TEST_CASE("Config: parallel tool workers are explicitly bounded", "[config]") {
    CHECK(agent::parse_config(R"({"limits":{"max_parallel_tools":1}})"));
    CHECK(agent::parse_config(R"({"limits":{"max_parallel_tools":32}})"));
    for (const auto* value : {"0", "-1", "33", "1.5"})
        CHECK_FALSE(agent::parse_config(std::string("{\"limits\":{\"max_parallel_tools\":") +
                                        value + "}}"));
}

TEST_CASE("Config: every configured section survives file loading", "[config]") {
    auto path = std::filesystem::temp_directory_path() /
                ("agent_config_" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::ofstream(path) << R"({
        "runtime":{"data_dir":"data","default_workspace":"work","jsonl_stdio":false},
        "model":{"provider":"openai","model":"local","api_key_env":"LOCAL_KEY","endpoint_env":"LOCAL_ENDPOINT"},
        "limits":{"max_prompt_bytes":1000,"max_transcript_bytes":2000,"max_tool_result_bytes":300,
                  "max_shell_output_bytes":400,"max_turn_seconds":5,"max_tool_seconds":2,
                  "max_concurrent_sessions":3,"max_cached_sessions":8,"max_parallel_tools":2},
        "tools":{"allow":["echo",7],"deny":["shell",false],"enable_dangerous":true,"allow_overwrite":true}
    })";
    auto cfg = agent::load_config(path.string());
    std::filesystem::remove(path);
    REQUIRE(cfg);
    CHECK(cfg->runtime.data_dir == "data");
    CHECK(cfg->runtime.default_workspace == "work");
    CHECK_FALSE(cfg->runtime.jsonl_stdio);
    CHECK(cfg->model.provider == "openai");
    CHECK(cfg->model.model == "local");
    CHECK(cfg->model.api_key_env == "LOCAL_KEY");
    CHECK(cfg->model.endpoint_env == "LOCAL_ENDPOINT");
    CHECK(cfg->limits.max_prompt_bytes == 1000);
    CHECK(cfg->limits.max_transcript_bytes == 2000);
    CHECK(cfg->limits.max_tool_result_bytes == 300);
    CHECK(cfg->limits.max_shell_output_bytes == 400);
    CHECK(cfg->limits.max_turn_seconds == 5);
    CHECK(cfg->limits.max_tool_seconds == 2);
    CHECK(cfg->limits.max_concurrent_sessions == 3);
    CHECK(cfg->limits.max_cached_sessions == 8);
    CHECK(cfg->limits.max_parallel_tools == 2);
    CHECK(cfg->tools.allow == std::vector<std::string>{"echo"});
    CHECK(cfg->tools.deny == std::vector<std::string>{"shell"});
    CHECK(cfg->tools.enable_dangerous);
    CHECK(cfg->tools.allow_overwrite);
    CHECK_FALSE(agent::load_config(path.string()));
    CHECK_FALSE(agent::parse_config("{"));
    CHECK_FALSE(agent::parse_config("[]"));
    CHECK_FALSE(agent::parse_config(R"({"limits":[]})"));
}
