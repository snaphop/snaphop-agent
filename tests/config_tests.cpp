#include <catch2/catch_test_macros.hpp>
#include "agent/config.hpp"

TEST_CASE("Config: rejects invalid limits", "[config]") {
    for (const auto* value : {"-1", "0", "1.5", "4294967296", "\"100\"", "null"}) {
        auto result =
            agent::parse_config(std::string("{\"limits\":{\"max_turn_seconds\":") + value + "}}");
        CHECK_FALSE(result.has_value());
    }
    CHECK(agent::parse_config(R"({"limits":{"max_turn_seconds":1}})"));
    CHECK(agent::parse_config("{}"));
}
