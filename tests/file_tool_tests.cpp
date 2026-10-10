#include <catch2/catch_test_macros.hpp>
#include "agent/config.hpp"
#include "agent/tool.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>

namespace agent {
std::unique_ptr<ITool> make_list_dir_tool();
std::unique_ptr<ITool> make_write_file_tool();
std::unique_ptr<ITool> make_read_file_tool();
} // namespace agent

namespace {
struct Directory {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("agent_file_tools_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Directory() {
        std::filesystem::create_directories(path);
    }
    ~Directory() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};
} // namespace

TEST_CASE("ListDir: reports files and directories within the serialized budget", "[tools]") {
    Directory dir;
    std::ofstream(dir.path / "hello.txt") << "hello";
    std::filesystem::create_directory(dir.path / "nested");
    auto tool = agent::make_list_dir_tool();
    CHECK(tool->definition().read_only);
    agent::ToolCall call{"list", "list_dir", agent::JsonValue::object()};
    agent::ToolContext ctx{.workspace = dir.path};
    auto result = tool->call(call, ctx);
    REQUIRE(result.ok);
    REQUIRE(result.content.size() == 2);
    for (const auto& item : result.content) {
        if (item["name"] == "hello.txt") {
            CHECK(item["type"] == "file");
            CHECK(item["size"] == 5);
        } else {
            CHECK(item["name"] == "nested");
            CHECK(item["type"] == "dir");
            CHECK(item["size"] == 0);
        }
    }
    agent::LimitsConfig limits;
    limits.max_tool_result_bytes = 2;
    ctx.limits = &limits;
    result = tool->call(call, ctx);
    CHECK(result.ok);
    CHECK(result.content.empty());
    CHECK(result.error == "Directory listing truncated");
    CHECK(agent::to_json_string(result.content).size() <= limits.max_tool_result_bytes);
    for (const auto* path : {"../outside", "/etc", "hello.txt", "missing"}) {
        call.arguments = {{"path", path}};
        CHECK_FALSE(tool->call(call, ctx).ok);
    }
    call.arguments = {{"path", "nested"}};
    result = tool->call(call, ctx);
    CHECK(result.ok);
    CHECK(result.content.empty());
}

TEST_CASE("File tools: filesystem failures and direct-call bounds fail safely", "[tools]") {
    Directory dir;
    std::ofstream(dir.path / "parent") << "file";
    auto writer = agent::make_write_file_tool();
    auto reader = agent::make_read_file_tool();
    CHECK(writer->definition().name == "write_file");
    CHECK(reader->definition().read_only);
    agent::ToolContext ctx{.workspace = dir.path, .allow_overwrite = true};
    for (const auto* path : {"../outside", "parent/nested", "."}) {
        agent::ToolCall call{
            "write", "write_file", {{"path", path}, {"content", "text"}, {"overwrite", true}}};
        CHECK_FALSE(writer->call(call, ctx).ok);
    }
    CHECK_FALSE(reader->call({"read", "read_file", {{"path", "missing"}}}, ctx).ok);
    for (const auto& args : {agent::JsonValue{{"path", "parent"}, {"offset", -1}},
                             agent::JsonValue{{"path", "parent"}, {"offset", UINT64_MAX}},
                             agent::JsonValue{{"path", "parent"}, {"limit", 0}},
                             agent::JsonValue{{"path", "parent"}, {"limit", "invalid"}},
                             agent::JsonValue{{"path", "."}}})
        CHECK_FALSE(reader->call({"read", "read_file", args}, ctx).ok);
    std::stop_source stop;
    stop.request_stop();
    ctx.stop = stop.get_token();
    CHECK_FALSE(reader->call({"read", "read_file", {{"path", "parent"}}}, ctx).ok);
}
