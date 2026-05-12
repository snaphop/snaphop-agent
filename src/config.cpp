#include "agent/config.hpp"
#include "agent/json.hpp"
#include <fstream>
#include <sstream>
#include <cstdlib>

namespace agent {

static void parse_runtime(const JsonValue& obj, RuntimeConfig& out) {
    if (auto v = json_string(obj, "data_dir"))         out.data_dir = *v;
    if (auto v = json_string(obj, "default_workspace")) out.default_workspace = *v;
    if (auto v = json_bool(obj, "jsonl_stdio"))         out.jsonl_stdio = *v;
}

static void parse_model(const JsonValue& obj, ModelConfig& out) {
    if (auto v = json_string(obj, "provider"))      out.provider = *v;
    if (auto v = json_string(obj, "model"))         out.model = *v;
    if (auto v = json_string(obj, "api_key_env"))   out.api_key_env = *v;
    if (auto v = json_string(obj, "endpoint_env"))  out.endpoint_env = *v;
}

static void parse_limits(const JsonValue& obj, LimitsConfig& out) {
    auto get_sz = [&](const char* k, size_t& field) {
        if (obj.contains(k) && obj[k].is_number()) field = obj[k].get<size_t>();
    };
    auto get_u32 = [&](const char* k, uint32_t& field) {
        if (obj.contains(k) && obj[k].is_number()) field = obj[k].get<uint32_t>();
    };
    get_sz("max_prompt_bytes",       out.max_prompt_bytes);
    get_sz("max_tool_result_bytes",  out.max_tool_result_bytes);
    get_sz("max_shell_output_bytes", out.max_shell_output_bytes);
    get_u32("max_turn_seconds",      out.max_turn_seconds);
    get_u32("max_tool_seconds",      out.max_tool_seconds);
    get_u32("max_concurrent_sessions", out.max_concurrent_sessions);
}

static void parse_tools(const JsonValue& obj, ToolsConfig& out) {
    if (obj.contains("allow") && obj["allow"].is_array()) {
        out.allow.clear();
        for (const auto& v : obj["allow"]) {
            if (v.is_string()) out.allow.push_back(v.get<std::string>());
        }
    }
    if (obj.contains("deny") && obj["deny"].is_array()) {
        out.deny.clear();
        for (const auto& v : obj["deny"]) {
            if (v.is_string()) out.deny.push_back(v.get<std::string>());
        }
    }
    if (auto v = json_bool(obj, "enable_dangerous")) out.enable_dangerous = *v;
}

std::expected<Config, std::string> parse_config(std::string_view json_text) {
    auto parsed = parse_json(json_text);
    if (!parsed) return std::unexpected("JSON parse error: " + parsed.error().message);

    const auto& root = *parsed;
    if (!root.is_object()) return std::unexpected("Config root must be a JSON object");

    Config cfg;
    if (root.contains("runtime") && root["runtime"].is_object()) parse_runtime(root["runtime"], cfg.runtime);
    if (root.contains("model")   && root["model"].is_object())   parse_model(root["model"],     cfg.model);
    if (root.contains("limits")  && root["limits"].is_object())  parse_limits(root["limits"],   cfg.limits);
    if (root.contains("tools")   && root["tools"].is_object())   parse_tools(root["tools"],     cfg.tools);
    return cfg;
}

std::expected<Config, std::string> load_config(const std::string& path) {
    std::ifstream f(path);
    if (!f) return std::unexpected("Cannot open config file: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return parse_config(ss.str());
}

void apply_env_overrides(Config& cfg) {
    // Resolve actual values from env; never written to disk or logs.
    (void)cfg;  // env resolved lazily when building model requests
}

} // namespace agent
