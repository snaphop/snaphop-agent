cmake_minimum_required(VERSION 3.20)
project(snaphop_agent VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Treat warnings as errors
add_compile_options(-Wall -Wextra -Wpedantic -Werror)

# Find dependencies
find_package(CURL REQUIRED)

include(FetchContent)

# nlohmann/json - single-header JSON library
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
FetchContent_MakeAvailable(nlohmann_json)

# Catch2 - unit test framework
FetchContent_Declare(
    Catch2
    URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.5.2.tar.gz
    URL_HASH SHA256=269543a49eb76f40b3f93ff231d4c24c27a7e16c90e47d2e45bcc564de470c6e
)
FetchContent_MakeAvailable(Catch2)

# Common library used by both targets
file(GLOB_RECURSE AGENT_LIB_SOURCES
    src/config.cpp
    src/event.cpp
    src/json.cpp
    src/session_manager.cpp
    src/transcript_store.cpp
    src/tool.cpp
    src/tool_policy.cpp
    src/prompt_builder.cpp
    src/model_client.cpp
    src/agent_runtime.cpp
    src/memory_manager.cpp
    src/tools/echo_tool.cpp
    src/tools/list_dir_tool.cpp
    src/tools/read_file_tool.cpp
    src/tools/write_file_tool.cpp
    src/tools/shell_tool.cpp
)

add_library(agent_lib STATIC ${AGENT_LIB_SOURCES})
target_include_directories(agent_lib PUBLIC include)
target_link_libraries(agent_lib PUBLIC nlohmann_json::nlohmann_json CURL::libcurl)

# agentd - headless JSONL runtime
add_executable(agentd src/main_agentd.cpp)
target_link_libraries(agentd PRIVATE agent_lib)

# agent_cli - interactive CLI wrapper
add_executable(agent_cli src/main_agent_cli.cpp)
target_link_libraries(agent_cli PRIVATE agent_lib)

# agent_tests - unit and integration tests
file(GLOB TEST_SOURCES tests/*.cpp)
add_executable(agent_tests ${TEST_SOURCES})
target_link_libraries(agent_tests PRIVATE agent_lib Catch2::Catch2WithMain)
target_include_directories(agent_tests PRIVATE include)

include(CTest)
include(Catch)
catch_discover_tests(agent_tests)
