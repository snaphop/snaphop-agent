#pragma once
#include <iosfwd>

#include "json.hpp"
#include <string>
#include <string_view>
#include <chrono>

namespace agent {

// Lifecycle phase values
constexpr std::string_view PHASE_START = "start";
constexpr std::string_view PHASE_END = "end";

// Event type tag strings
constexpr std::string_view EVT_LIFECYCLE = "lifecycle";
constexpr std::string_view EVT_ASSISTANT_DELTA = "assistant_delta";
constexpr std::string_view EVT_ASSISTANT_FINAL = "assistant_final";
constexpr std::string_view EVT_TOOL_START = "tool_start";
constexpr std::string_view EVT_TOOL_END = "tool_end";
constexpr std::string_view EVT_ERROR = "error";

// Transcript-level event types
constexpr std::string_view TEVT_USER_MESSAGE = "user_message";
constexpr std::string_view TEVT_ASSISTANT_MESSAGE = "assistant_message";
constexpr std::string_view TEVT_TOOL_CALL = "tool_call";
constexpr std::string_view TEVT_TOOL_RESULT = "tool_result";
constexpr std::string_view TEVT_SYSTEM_NOTE = "system_note";
constexpr std::string_view TEVT_COMPACTION_SUMMARY = "compaction_summary";
constexpr std::string_view TEVT_ERROR = "error";

// Return RFC3339 timestamp for current UTC time.
std::string now_iso8601();

// Build a runtime event as JsonValue ready to emit to stdout.
JsonValue make_lifecycle_event(std::string_view session_id, std::string_view turn_id,
                               std::string_view phase);
JsonValue make_assistant_delta(std::string_view text);
JsonValue make_assistant_final(std::string_view text, std::string_view session_id,
                               std::string_view turn_id);
JsonValue make_tool_start(std::string_view tool_name, std::string_view call_id);
JsonValue make_tool_end(std::string_view tool_name, std::string_view call_id, bool ok,
                        std::string_view error = "");
JsonValue make_error_event(std::string_view message, std::string_view session_id = "",
                           std::string_view turn_id = "");

// Build a transcript record.
JsonValue make_transcript_user(std::string_view turn_id, std::string_view text);
JsonValue make_transcript_assistant(std::string_view turn_id, std::string_view text);
JsonValue make_transcript_tool_call(std::string_view turn_id, std::string_view call_id,
                                    std::string_view tool_name, const JsonValue& arguments);
JsonValue make_transcript_tool_result(std::string_view turn_id, std::string_view call_id, bool ok,
                                      const JsonValue& content, std::string_view error = "");
JsonValue make_transcript_system_note(std::string_view text);
JsonValue make_transcript_compaction(uint64_t covered_until_event, std::string_view summary);
JsonValue make_transcript_error(std::string_view turn_id, std::string_view message);

// Emit a JsonValue as a JSONL line to stdout.
// Select the process event stream at startup (stdout by default).
void set_event_stream(std::ostream& stream);
void emit_event(const JsonValue& evt);

} // namespace agent
