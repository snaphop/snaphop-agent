#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <optional>
#include <expected>

namespace agent {

using JsonValue = nlohmann::json;

struct ParseError {
    std::string message;
};

// Parse a JSON string, returning an error on failure.
inline std::expected<JsonValue, ParseError> parse_json(std::string_view text) {
    try {
        return JsonValue::parse(text);
    } catch (const nlohmann::json::parse_error& e) {
        return std::unexpected(ParseError{e.what()});
    }
}

// Serialize JSON to a compact string.
inline std::string to_json_string(const JsonValue& v) {
    return v.dump();
}

// Get a string field, returning nullopt if missing or wrong type.
inline std::optional<std::string> json_string(const JsonValue& v, std::string_view key) {
    auto k = std::string(key);
    if (!v.is_object() || !v.contains(k)) return std::nullopt;
    const auto& f = v[k];
    if (!f.is_string()) return std::nullopt;
    return f.get<std::string>();
}

// Get a bool field, returning nullopt if missing or wrong type.
inline std::optional<bool> json_bool(const JsonValue& v, std::string_view key) {
    auto k = std::string(key);
    if (!v.is_object() || !v.contains(k)) return std::nullopt;
    const auto& f = v[k];
    if (!f.is_boolean()) return std::nullopt;
    return f.get<bool>();
}

// Get an int field, returning nullopt if missing or wrong type.
inline std::optional<int64_t> json_int(const JsonValue& v, std::string_view key) {
    auto k = std::string(key);
    if (!v.is_object() || !v.contains(k)) return std::nullopt;
    const auto& f = v[k];
    if (!f.is_number_integer()) return std::nullopt;
    return f.get<int64_t>();
}

} // namespace agent
