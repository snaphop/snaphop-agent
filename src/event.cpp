#include "agent/event.hpp"
#include <iostream>
#include <chrono>
#include <format>
#include <mutex>

namespace agent {

std::string now_iso8601() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto tt = system_clock::to_time_t(now);
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    char buf[32];
    std::tm tm_utc{};
    gmtime_r(&tt, &tm_utc);
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_utc);
    return std::string(buf) + "." + std::to_string(ms.count()) + "Z";
}

static JsonValue base_event(std::string_view type) {
    JsonValue e;
    e["ts"]   = now_iso8601();
    e["type"] = std::string(type);
    return e;
}

JsonValue make_lifecycle_event(std::string_view session_id, std::string_view turn_id, std::string_view phase) {
    auto e = base_event(EVT_LIFECYCLE);
    e["session_id"] = std::string(session_id);
    e["turn_id"]    = std::string(turn_id);
    e["phase"]      = std::string(phase);
    return e;
}

JsonValue make_assistant_delta(std::string_view text) {
    auto e = base_event(EVT_ASSISTANT_DELTA);
    e["text"] = std::string(text);
    return e;
}

JsonValue make_assistant_final(std::string_view text, std::string_view session_id, std::string_view turn_id) {
    auto e = base_event(EVT_ASSISTANT_FINAL);
    e["text"]       = std::string(text);
    e["session_id"] = std::string(session_id);
    e["turn_id"]    = std::string(turn_id);
    return e;
}

JsonValue make_tool_start(std::string_view tool_name, std::string_view call_id) {
    auto e = base_event(EVT_TOOL_START);
    e["tool"]    = std::string(tool_name);
    e["call_id"] = std::string(call_id);
    return e;
}

JsonValue make_tool_end(std::string_view tool_name, std::string_view call_id, bool ok, std::string_view error) {
    auto e = base_event(EVT_TOOL_END);
    e["tool"]    = std::string(tool_name);
    e["call_id"] = std::string(call_id);
    e["ok"]      = ok;
    if (!error.empty()) e["error"] = std::string(error);
    return e;
}

JsonValue make_error_event(std::string_view message, std::string_view session_id, std::string_view turn_id) {
    auto e = base_event(EVT_ERROR);
    e["message"] = std::string(message);
    if (!session_id.empty()) e["session_id"] = std::string(session_id);
    if (!turn_id.empty())    e["turn_id"]    = std::string(turn_id);
    return e;
}

JsonValue make_transcript_user(std::string_view turn_id, std::string_view text) {
    auto e = base_event(TEVT_USER_MESSAGE);
    e["turn_id"] = std::string(turn_id);
    e["text"]    = std::string(text);
    return e;
}

JsonValue make_transcript_assistant(std::string_view turn_id, std::string_view text) {
    auto e = base_event(TEVT_ASSISTANT_MESSAGE);
    e["turn_id"] = std::string(turn_id);
    e["text"]    = std::string(text);
    return e;
}

JsonValue make_transcript_tool_call(std::string_view turn_id, std::string_view call_id,
                                    std::string_view tool_name, const JsonValue& arguments) {
    auto e = base_event(TEVT_TOOL_CALL);
    e["turn_id"]   = std::string(turn_id);
    e["call_id"]   = std::string(call_id);
    e["tool"]      = std::string(tool_name);
    e["arguments"] = arguments;
    return e;
}

JsonValue make_transcript_tool_result(std::string_view turn_id, std::string_view call_id,
                                      bool ok, const JsonValue& content, std::string_view error) {
    auto e = base_event(TEVT_TOOL_RESULT);
    e["turn_id"]  = std::string(turn_id);
    e["call_id"]  = std::string(call_id);
    e["ok"]       = ok;
    e["content"]  = content;
    if (!error.empty()) e["error"] = std::string(error);
    return e;
}

JsonValue make_transcript_system_note(std::string_view text) {
    auto e = base_event(TEVT_SYSTEM_NOTE);
    e["text"] = std::string(text);
    return e;
}

JsonValue make_transcript_compaction(uint64_t covered_until_event, std::string_view summary) {
    auto e = base_event(TEVT_COMPACTION_SUMMARY);
    e["covered_until_event"] = covered_until_event;
    e["summary"]             = std::string(summary);
    return e;
}

JsonValue make_transcript_error(std::string_view turn_id, std::string_view message) {
    auto e = base_event(TEVT_ERROR);
    e["turn_id"] = std::string(turn_id);
    e["message"] = std::string(message);
    return e;
}

static std::mutex emit_mutex;

void emit_event(const JsonValue& evt) {
    auto line = to_json_string(evt);
    std::lock_guard<std::mutex> lock(emit_mutex);
    std::cout << line << '\n' << std::flush;
}

} // namespace agent
