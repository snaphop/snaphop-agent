#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <expected>

namespace agent {

struct ParseError;

struct RuntimeConfig {
    std::string data_dir = ".agent";
    std::string default_workspace = "workspace";
    bool jsonl_stdio = true;
};

struct ModelConfig {
    std::string provider = "generic_http";
    std::string model;
    std::string api_key_env = "AGENT_API_KEY";
    std::string endpoint_env = "AGENT_ENDPOINT";
};

struct LimitsConfig {
    size_t max_prompt_bytes = 262144;
    size_t max_transcript_bytes = 4194304;
    size_t max_tool_result_bytes = 65536;
    size_t max_shell_output_bytes = 65536;
    uint32_t max_turn_seconds = 300;
    uint32_t max_tool_seconds = 30;
    uint32_t max_concurrent_sessions = 4;
    uint32_t max_cached_sessions = 64;
};

struct ToolsConfig {
    std::vector<std::string> allow = {"echo", "list_dir", "read_file", "write_file"};
    std::vector<std::string> deny = {"shell"};
    bool enable_dangerous = false;
    bool allow_overwrite = false;
};

struct Config {
    RuntimeConfig runtime;
    ModelConfig model;
    LimitsConfig limits;
    ToolsConfig tools;
};

// Load config from a JSON file path. Returns error string on failure.
std::expected<Config, std::string> load_config(const std::string& path);

// Load config from a JSON string.
std::expected<Config, std::string> parse_config(std::string_view json_text);

// Apply environment variable overrides for sensitive fields.
void apply_env_overrides(Config& cfg);

} // namespace agent
