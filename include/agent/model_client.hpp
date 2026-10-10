#pragma once

#include "json.hpp"
#include "tool.hpp"
#include <string>
#include <vector>
#include <functional>
#include <stop_token>
#include <expected>

namespace agent {

struct ModelToolCall {
    std::string call_id;
    std::string name;
    JsonValue arguments;
};

// A single message in the conversation (simplified for the model request).
struct Message {
    std::string role; // "user", "assistant", "tool"
    std::string content;
    std::string tool_call_id;
    std::string tool_name;
    std::vector<ModelToolCall> tool_calls = {};
    bool tool_error = false;
};

struct ModelRequest {
    std::string system_prompt;
    std::vector<Message> messages;
    std::vector<ToolDefinition> tools;
    // Immutable canonical schemas prepared once per turn; adapters own projection.
    JsonValue tool_schemas = JsonValue::array();
    std::string model;
    float temperature = 0.0f;
    size_t max_output_tokens = 4096;
    // API credentials (resolved from env at runtime, not stored in transcripts)
    std::string api_key;
    std::string endpoint;
    size_t max_response_bytes = 1048576;
    uint32_t timeout_seconds = 120;
};

enum class FinishReason {
    Stop,
    ToolUse,
    MaxTokens,
    Error,
    Cancelled,
};

struct ModelResponse {
    std::string text;
    std::vector<ModelToolCall> tool_calls;
    FinishReason finish_reason = FinishReason::Stop;
    // Usage metadata (optional; safe to log)
    int input_tokens = 0;
    int output_tokens = 0;
    std::string provider_id;
    std::string error_message;
};

// Delta callback for streaming. Returns false to cancel.
using DeltaCallback = std::function<bool(std::string_view delta)>;

class IModelClient {
  public:
    virtual ~IModelClient() = default;

    // Perform a model completion. Calls delta_cb for each text chunk if non-null.
    virtual ModelResponse complete(const ModelRequest& request, std::stop_token stop,
                                   DeltaCallback delta_cb = nullptr) = 0;
};

// Pure serialization helpers used by adapters and protocol regression tests.
JsonValue anthropic_request_body(const ModelRequest& request);
JsonValue openai_request_body(const ModelRequest& request);

// Anthropic API adapter (Claude via Messages API).
std::unique_ptr<IModelClient> make_anthropic_client();

// Generic OpenAI-compatible HTTP adapter.
std::unique_ptr<IModelClient> make_openai_client();

} // namespace agent
