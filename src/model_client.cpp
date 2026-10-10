#include "agent/model_client.hpp"
#include "agent/json.hpp"
#include <curl/curl.h>
#include <sstream>
#include <iostream>
#include <atomic>
#include <mutex>

namespace agent {

JsonValue anthropic_request_body(const ModelRequest& req) {
    // Build request body.
    JsonValue body;
    body["model"] = req.model;
    body["max_tokens"] = static_cast<int>(req.max_output_tokens);

    // Messages
    JsonValue messages = JsonValue::array();
    for (const auto& m : req.messages) {
        const std::string role = m.role == "assistant" ? "assistant" : "user";
        JsonValue blocks = JsonValue::array();
        if (m.role == "tool") {
            blocks.push_back({{"type", "tool_result"},
                              {"tool_use_id", m.tool_call_id},
                              {"content", m.content},
                              {"is_error", m.tool_error}});
        } else {
            if (!m.content.empty())
                blocks.push_back({{"type", "text"}, {"text", m.content}});
            for (const auto& tc : m.tool_calls)
                blocks.push_back({{"type", "tool_use"},
                                  {"id", tc.call_id},
                                  {"name", tc.name},
                                  {"input", tc.arguments}});
        }
        if (!messages.empty() && messages.back()["role"] == role) {
            for (auto& block : blocks)
                messages.back()["content"].push_back(std::move(block));
        } else {
            messages.push_back({{"role", role}, {"content", std::move(blocks)}});
        }
    }
    body["messages"] = std::move(messages);
    body["system"] = req.system_prompt;

    // Tools
    if (!req.tools.empty()) {
        JsonValue tools_json = JsonValue::array();
        for (const auto& t : req.tools) {
            JsonValue tj;
            tj["name"] = t.name;
            tj["description"] = t.description;
            const auto index = tools_json.size();
            tj["input_schema"] = req.tool_schemas.size() == req.tools.size()
                                     ? req.tool_schemas[index]
                                     : tool_input_schema(t);
            tools_json.push_back(std::move(tj));
        }
        body["tools"] = std::move(tools_json);
    }

    return body;
}

JsonValue openai_request_body(const ModelRequest& req) {
    JsonValue body;
    body["model"] = req.model;
    body["max_tokens"] = static_cast<int>(req.max_output_tokens);
    body["temperature"] = req.temperature;

    JsonValue messages = JsonValue::array();
    if (!req.system_prompt.empty()) {
        JsonValue sys_msg;
        sys_msg["role"] = "system";
        sys_msg["content"] = req.system_prompt;
        messages.push_back(std::move(sys_msg));
    }
    for (const auto& m : req.messages) {
        JsonValue msg;
        msg["role"] = m.role;
        msg["content"] = m.content;
        if (m.role == "tool")
            msg["tool_call_id"] = m.tool_call_id;
        if (!m.tool_calls.empty()) {
            msg["tool_calls"] = JsonValue::array();
            for (const auto& tc : m.tool_calls)
                msg["tool_calls"].push_back(
                    {{"id", tc.call_id},
                     {"type", "function"},
                     {"function",
                      {{"name", tc.name}, {"arguments", to_json_string(tc.arguments)}}}});
        }
        messages.push_back(std::move(msg));
    }
    body["messages"] = std::move(messages);

    if (!req.tools.empty()) {
        JsonValue tools_json = JsonValue::array();
        for (const auto& t : req.tools) {
            JsonValue tj;
            tj["type"] = "function";
            JsonValue fn;
            fn["name"] = t.name;
            fn["description"] = t.description;
            const auto index = tools_json.size();
            fn["parameters"] = req.tool_schemas.size() == req.tools.size() ? req.tool_schemas[index]
                                                                           : tool_input_schema(t);
            tj["function"] = std::move(fn);
            tools_json.push_back(std::move(tj));
        }
        body["tools"] = std::move(tools_json);
    }

    return body;
}

namespace {
struct Transfer {
    std::string body;
    size_t limit;
    std::stop_token stop;
};
size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto& transfer = *static_cast<Transfer*>(userdata);
    const auto bytes = size * nmemb;
    if (transfer.stop.stop_requested() || bytes > transfer.limit - transfer.body.size())
        return 0;
    transfer.body.append(ptr, bytes);
    return bytes;
}
int progress_cb(void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return static_cast<Transfer*>(userdata)->stop.stop_requested() ? 1 : 0;
}
// Initialized once; easy handles own all per-request state. Cleanup occurs at process exit.
struct CurlGlobal {
    CurlGlobal() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }
    ~CurlGlobal() {
        curl_global_cleanup();
    }
};
void ensure_curl() {
    static const CurlGlobal global;
}
} // namespace

// ---- Anthropic Messages API adapter ----

class AnthropicClient : public IModelClient {
  public:
    AnthropicClient() {
        ensure_curl();
    }

    ModelResponse complete(const ModelRequest& req, std::stop_token stop,
                           DeltaCallback delta_cb) override {
        ModelResponse resp;

        if (stop.stop_requested()) {
            resp.finish_reason = FinishReason::Cancelled;
            return resp;
        }
        if (req.model.empty()) {
            resp.finish_reason = FinishReason::Error;
            resp.error_message = "model.model must be configured";
            return resp;
        }
        if (req.api_key.empty()) {
            resp.finish_reason = FinishReason::Error;
            resp.error_message = "API key not set (check AGENT_API_KEY env var)";
            return resp;
        }

        std::string endpoint =
            req.endpoint.empty() ? "https://api.anthropic.com/v1/messages" : req.endpoint;

        auto body = anthropic_request_body(req);

        std::string body_str = to_json_string(body);

        CURL* curl = curl_easy_init();
        if (!curl) {
            resp.finish_reason = FinishReason::Error;
            resp.error_message = "curl_easy_init failed";
            return resp;
        }

        Transfer transfer{{}, req.max_response_bytes, stop};
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("x-api-key: " + req.api_key).c_str());
        headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
        headers = curl_slist_append(headers, "content-type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body_str.size()));
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(req.timeout_seconds));
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &transfer);

        CURLcode res = curl_easy_perform(curl);
        long status_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        if (status_code >= 400) {
            resp.finish_reason = FinishReason::Error;
            resp.error_message = "Provider HTTP status " + std::to_string(status_code);
            return resp;
        }

        if (res != CURLE_OK) {
            resp.finish_reason =
                stop.stop_requested() ? FinishReason::Cancelled : FinishReason::Error;
            resp.error_message = std::string("curl error: ") + curl_easy_strerror(res);
            return resp;
        }

        if (stop.stop_requested()) {
            resp.finish_reason = FinishReason::Cancelled;
            return resp;
        }

        auto parsed = parse_json(transfer.body);
        if (!parsed) {
            resp.finish_reason = FinishReason::Error;
            resp.error_message = "Failed to parse API response";
            return resp;
        }

        const auto& r = *parsed;
        resp.provider_id = r.value("id", "");

        if (r.contains("error")) {
            resp.finish_reason = FinishReason::Error;
            resp.error_message = "Provider returned an API error";
            return resp;
        }

        // Usage
        if (r.contains("usage") && r["usage"].is_object()) {
            resp.input_tokens = r["usage"].value("input_tokens", 0);
            resp.output_tokens = r["usage"].value("output_tokens", 0);
        }

        // Stop reason
        std::string stop_reason = r.value("stop_reason", "end_turn");
        if (stop_reason == "tool_use")
            resp.finish_reason = FinishReason::ToolUse;
        else if (stop_reason == "max_tokens")
            resp.finish_reason = FinishReason::MaxTokens;
        else
            resp.finish_reason = FinishReason::Stop;

        if (!r.contains("content") || !r["content"].is_array() || r["content"].empty()) {
            resp.finish_reason = FinishReason::Error;
            resp.error_message = "Provider response has no content";
            return resp;
        }
        // Content blocks
        if (r.contains("content") && r["content"].is_array()) {
            for (const auto& block : r["content"]) {
                std::string btype = block.value("type", "");
                if (btype == "text") {
                    const auto text = block.value("text", "");
                    resp.text += text;
                    if (delta_cb && !delta_cb(text)) {
                        resp.finish_reason = FinishReason::Cancelled;
                        return resp;
                    }
                } else if (btype == "tool_use") {
                    ModelToolCall tc;
                    tc.call_id = block.value("id", "");
                    tc.name = block.value("name", "");
                    tc.arguments = block.contains("input") ? block["input"] : JsonValue::object();
                    resp.tool_calls.push_back(std::move(tc));
                }
            }
        }

        return resp;
    }
};

// ---- OpenAI-compatible adapter ----

class OpenAIClient : public IModelClient {
  public:
    OpenAIClient() {
        ensure_curl();
    }

    ModelResponse complete(const ModelRequest& req, std::stop_token stop,
                           DeltaCallback delta_cb) override {
        ModelResponse resp;
        if (stop.stop_requested()) {
            resp.finish_reason = FinishReason::Cancelled;
            return resp;
        }
        if (req.model.empty()) {
            resp.finish_reason = FinishReason::Error;
            resp.error_message = "model.model must be configured";
            return resp;
        }
        if (req.api_key.empty()) {
            resp.finish_reason = FinishReason::Error;
            resp.error_message = "API key not set";
            return resp;
        }

        std::string endpoint =
            req.endpoint.empty() ? "https://api.openai.com/v1/chat/completions" : req.endpoint;

        auto body = openai_request_body(req);

        std::string body_str = to_json_string(body);
        Transfer transfer{{}, req.max_response_bytes, stop};

        CURL* curl = curl_easy_init();
        if (!curl) {
            resp.finish_reason = FinishReason::Error;
            resp.error_message = "curl_easy_init failed";
            return resp;
        }

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("Authorization: Bearer " + req.api_key).c_str());
        headers = curl_slist_append(headers, "content-type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body_str.size()));
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(req.timeout_seconds));
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &transfer);

        CURLcode res = curl_easy_perform(curl);
        long status_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        if (status_code >= 400) {
            resp.finish_reason = FinishReason::Error;
            resp.error_message = "Provider HTTP status " + std::to_string(status_code);
            return resp;
        }

        if (res != CURLE_OK) {
            resp.finish_reason =
                stop.stop_requested() ? FinishReason::Cancelled : FinishReason::Error;
            resp.error_message = std::string("curl error: ") + curl_easy_strerror(res);
            return resp;
        }

        if (stop.stop_requested()) {
            resp.finish_reason = FinishReason::Cancelled;
            return resp;
        }

        auto parsed = parse_json(transfer.body);
        if (!parsed) {
            resp.finish_reason = FinishReason::Error;
            resp.error_message = "Failed to parse response";
            return resp;
        }

        const auto& r = *parsed;
        if (r.contains("error")) {
            resp.finish_reason = FinishReason::Error;
            resp.error_message = "Provider returned an API error";
            return resp;
        }

        if (r.contains("usage") && r["usage"].is_object()) {
            resp.input_tokens = r["usage"].value("prompt_tokens", 0);
            resp.output_tokens = r["usage"].value("completion_tokens", 0);
        }

        if (!r.contains("choices") || !r["choices"].is_array() || r["choices"].empty()) {
            resp.finish_reason = FinishReason::Error;
            resp.error_message = "Provider response has no choices";
            return resp;
        }
        if (r.contains("choices") && r["choices"].is_array() && !r["choices"].empty()) {
            const auto& choice = r["choices"][0];
            std::string finish = choice.value("finish_reason", "stop");
            if (finish == "tool_calls")
                resp.finish_reason = FinishReason::ToolUse;
            else if (finish == "length")
                resp.finish_reason = FinishReason::MaxTokens;
            else
                resp.finish_reason = FinishReason::Stop;

            if (choice.contains("message") && choice["message"].is_object()) {
                const auto& msg = choice["message"];
                resp.text = msg.contains("content") && msg["content"].is_string()
                                ? msg["content"].get<std::string>()
                                : "";
                if (delta_cb && !resp.text.empty() && !delta_cb(resp.text)) {
                    resp.finish_reason = FinishReason::Cancelled;
                    return resp;
                }

                if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
                    for (const auto& tc : msg["tool_calls"]) {
                        ModelToolCall call;
                        call.call_id = tc.value("id", "");
                        if (tc.contains("function") && tc["function"].is_object()) {
                            call.name = tc["function"].value("name", "");
                            std::string args_str = tc["function"].value("arguments", "{}");
                            auto parsed_args = parse_json(args_str);
                            call.arguments = parsed_args ? *parsed_args : JsonValue(nullptr);
                        }
                        resp.tool_calls.push_back(std::move(call));
                    }
                }
            }
        }

        return resp;
    }
};

std::unique_ptr<IModelClient> make_anthropic_client() {
    return std::make_unique<AnthropicClient>();
}

std::unique_ptr<IModelClient> make_openai_client() {
    return std::make_unique<OpenAIClient>();
}

} // namespace agent
