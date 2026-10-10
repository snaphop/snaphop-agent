cmake_minimum_required(VERSION 3.20)
project(snaphop_agent VERSION 0.1.0 LANGUAGES CXX)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
include(CTest)
find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AGENT_ENABLE_COVERAGE "Instrument all agent sources for GCC/gcov coverage" OFF)
if(AGENT_ENABLE_COVERAGE)
    if(NOT BUILD_TESTING)
        message(FATAL_ERROR "AGENT_ENABLE_COVERAGE requires BUILD_TESTING=ON")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "Coverage builds currently require GCC and its matching gcov")
    endif()
    add_library(agent_coverage INTERFACE)
    target_compile_options(agent_coverage INTERFACE --coverage -O0 -g -fprofile-update=atomic)
    target_link_options(agent_coverage INTERFACE --coverage)
endif()

# Treat warnings as errors
add_compile_options(-Wall -Wextra -Wpedantic -Werror)

# Find dependencies
find_package(CURL REQUIRED)

include(FetchContent)

# nlohmann/json - single-header JSON library
FetchContent_Declare(
    nlohmann_json
    URL https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.12.0
    URL_HASH SHA256=4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187
)
FetchContent_MakeAvailable(nlohmann_json)

# Common library used by both targets
set(AGENT_LIB_SOURCES
    src/config.cpp
    src/event.cpp
    src/json.cpp
    src/session_manager.cpp
    src/transcript_store.cpp
    src/tool_executor.cpp
    src/tool_loop_guard.cpp
    src/tool.cpp
    src/tool_result_budget.cpp
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
target_link_libraries(agent_lib PUBLIC nlohmann_json::nlohmann_json CURL::libcurl Threads::Threads)
if(AGENT_ENABLE_COVERAGE)
    target_link_libraries(agent_lib PUBLIC agent_coverage)
endif()

# agentd - headless JSONL runtime
add_executable(agentd src/main_agentd.cpp)
target_link_libraries(agentd PRIVATE agent_lib)

# agent_cli - interactive CLI wrapper
add_executable(agent_cli src/main_agent_cli.cpp)
target_link_libraries(agent_cli PRIVATE agent_lib)

if(BUILD_TESTING)
    FetchContent_Declare(
        Catch2
        URL https://codeload.github.com/catchorg/Catch2/tar.gz/refs/tags/v3.16.1
        URL_HASH SHA256=d16bd1b8b2364918bd472cb216394acb93bde6784890df0d91bccfb3f28d777d
    )
    FetchContent_MakeAvailable(Catch2)
    file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
    add_executable(agent_tests ${TEST_SOURCES})
    target_link_libraries(agent_tests PRIVATE agent_lib Catch2::Catch2WithMain)
    include(Catch)
    catch_discover_tests(agent_tests)
endif()
