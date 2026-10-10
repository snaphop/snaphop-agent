#include <catch2/catch_test_macros.hpp>
#include "agent/tool_result_budget.hpp"

TEST_CASE("Tool result budget preserves command metadata and diagnostic tail", "[tools]") {
    agent::JsonValue content{{"stdout", std::string(1000, 'x') + "\nFAILED: bad input"},
                             {"stderr", "diagnostic"},
                             {"exit_code", 7},
                             {"timed_out", false}};
    const auto bounded = agent::bound_tool_content(content, 240);
    REQUIRE(bounded.is_object());
    CHECK(agent::to_json_string(bounded).size() <= 240);
    CHECK(bounded["exit_code"] == 7);
    CHECK(bounded["stderr"] == "diagnostic");
    CHECK(bounded["truncated"] == true);
    CHECK(bounded["stdout"].get<std::string>().ends_with("FAILED: bad input"));
}

TEST_CASE("Tool result budget handles escaping UTF-8 multiple streams and tiny limits", "[tools]") {
    std::string text;
    for (int i = 0; i < 200; ++i)
        text += "\"\\\n世界";
    for (size_t limit : {1u, 16u, 64u, 128u, 256u}) {
        for (const auto& input :
             {agent::JsonValue(text),
              agent::JsonValue{{"stdout", text}, {"stderr", text}, {"exit_code", 1}}}) {
            const auto bounded = agent::bound_tool_content(input, limit);
            CHECK(agent::to_json_string(bounded).size() <= limit);
        }
    }
}

TEST_CASE("Tool result budget retains bounded array prefix and leaves small results intact",
          "[tools]") {
    const agent::JsonValue small{{"content", "hello"}, {"path", "file.txt"}};
    CHECK(agent::bound_tool_content(small, 100) == small);
    const agent::JsonValue entries = agent::JsonValue::array({small, small, small});
    const auto bounded = agent::bound_tool_content(entries, 80);
    REQUIRE(bounded.is_object());
    REQUIRE_FALSE(bounded["items"].empty());
    CHECK(bounded["items"][0] == small);
    CHECK(bounded["truncated"] == true);
    CHECK(agent::to_json_string(bounded).size() <= 80);
}
