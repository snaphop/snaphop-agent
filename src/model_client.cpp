#include "agent/model_client.hpp"
#include "agent/json.hpp"
#include <curl/curl.h>
#include <sstream>
#include <iostream>
#include <atomic>
#include <mutex>

namespace agent {

// ---- curl write callback ----
static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::string*>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

// ---- Anthropic Messages API adapter ----

class AnthropicClient : public IModelClient {
public:
    AnthropicClient() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }
    ~AnthropicClient() {
        curl_global_cleanup();
    }

    ModelResponse complete(
        const ModelRequest& req,
        std::stop_token stop,
        DeltaCallback delta_cb) override
    {
        ModelResponse resp;

        if (req.api_key.empty()) {
            resp.finish_reason = FinishReason::Error;
            resp.error_message = "API key not set (check AGENT_API_KEY env var)";
            return resp;
        }

        std::string endpoint = req.endpoint.empty()
            ? "https://api.anthropic.com/v1/messages"
            : req.endpoint;

        // Build request body.
        JsonValue body;
        body["model"]      = req.model.empty() ? "claude-opus-4-7-20251101" : req.model;
        body["max_tokens"] = static_cast<int>(req.max_output_tokens);

        // Messages
        JsonValue messages = JsonValue::array();
        for (const auto& m : req.messages) {
            JsonValue msg;
            msg["role"]    = m.role == "assistant" ? "assistant" : "user";
            msg["content"] = m.content;
            messages.push_back(std::move(msg));
        }
        body["messages"] = std::move(messages);
        body["system"]   = req.system_prompt;

        // Tools
        if (!req.tools.empty()) {
            JsonValue tools_json = JsonValue::array();
            for (const auto& t : req.tools) {
                JsonValue tj;
                tj["name"]        = t.name;
                tj["description"] = t.description;
                JsonValue input_schema;
                input_schema["type"] = "object";
                JsonValue props = JsonValue::object();
                JsonValue required_arr = JsonValue::array();
                for (const auto& p : t.params) {
                    JsonValue pj;
                    pj["type"]        = p.type;
                    pj["description"] = p.description;
                    props[p.name]     = std::move(pj);
                    if (p.required) required_arr.push_back(p.name);
                }
                input_schema["properties"] = std::move(props);
                input_schema["required"]   = std::move(required_arr);
                tj["input_schema"]         = std::move(input_schema);
                tools_json.push_back(std::move(tj));
            }
            body["tools"] = std::move(tools_json);
        }

        std::string body_str = to_json_string(body);

        CURL* curl = curl_easy_init();
        if (!curl) {
            resp.finish_reason = FinishReason::Error;
            resp.error_message = "curl_easy_init failed";
            return resp;
        }

        std::string response_body;
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("x-api-key: " + req.api_key).c_str());
        headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
        headers = curl_slist_append(headers, "content-type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body_str.size()));
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            resp.finish_reason = FinishReason::Error;
            resp.error_message = std::string("curl error: ") + curl_easy_strerror(res);
            return resp;
        }

        if (stop.stop_requested()) {
            resp.finish_reason = FinishReason::Cancelled;
            return resp;
        }

        auto parsed = parse_json(response_body);
        if (!parsed) {
            resp.finish_reason = FinishReason::Error;
            resp.error_message = "Failed to parse API response: " + parsed.error().message;
            return resp;
        }

        const auto& r = *parsed;
        resp.provider_id = r.value("id", "");

        if (r.contains("error")) {
            resp.finish_reason = FinishReason::Error;
            resp.error_message = r["error"].value("message", "Unknown API error");
            return resp;
        }

        // Usage
        if (r.contains("usage") && r["usage"].is_object()) {
            resp.input_tokens  = r["usage"].value("input_tokens",  0);
            resp.output_tokens = r["usage"].value("output_tokens", 0);
        }

        // Stop reason
        std::string stop_reason = r.value("stop_reason", "end_turn");
        if (stop_reason == "tool_use")    resp.finish_reason = FinishReason::ToolUse;
        else if (stop_reason == "max_tokens") resp.finish_reason = FinishReason::MaxTokens;
        else                               resp.finish_reason = FinishReason::Stop;

        // Content blocks
        if (r.contains("content") && r["content"].is_array()) {
            for (const auto& block : r["content"]) {
                std::string btype = block.value("type", "");
                if (btype == "text") {
                    resp.text += block.value("text", "");
                    if (delta_cb) delta_cb(resp.text);
                } else if (btype == "tool_use") {
                    ModelToolCall tc;
                    tc.call_id   = block.value("id", "");
                    tc.name      = block.value("name", "");
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
    OpenAIClient() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~OpenAIClient() { curl_global_cleanup(); }

    ModelResponse complete(
        const ModelRequest& req,
        std::stop_token stop,
        DeltaCallback delta_cb) override
    {
        ModelResponse resp;
        if (req.api_key.empty()) {
            resp.finish_reason = FinishReason::Error;
            resp.error_message = "API key not set";
            return resp;
        }

        std::string endpoint = req.endpoint.empty()
            ? "https://api.openai.com/v1/chat/completions"
            : req.endpoint;

        JsonValue body;
        body["model"]      = req.model;
        body["max_tokens"] = static_cast<int>(req.max_output_tokens);
        body["temperature"] = req.temperature;

        JsonValue messages = JsonValue::array();
        if (!req.system_prompt.empty()) {
            JsonValue sys_msg;
            sys_msg["role"]    = "system";
            sys_msg["content"] = req.system_prompt;
            messages.push_back(std::move(sys_msg));
        }
        for (const auto& m : req.messages) {
            JsonValue msg;
            msg["role"]    = m.role;
            msg["content"] = m.content;
            messages.push_back(std::move(msg));
        }
        body["messages"] = std::move(messages);

        if (!req.tools.empty()) {
            JsonValue tools_json = JsonValue::array();
            for (const auto& t : req.tools) {
                JsonValue tj;
                tj["type"] = "function";
                JsonValue fn;
                fn["name"]        = t.name;
                fn["description"] = t.description;
                JsonValue params_schema;
                params_schema["type"] = "object";
                JsonValue props = JsonValue::object();
                JsonValue required_arr = JsonValue::array();
                for (const auto& p : t.params) {
                    JsonValue pj;
                    pj["type"]        = p.type;
                    pj["description"] = p.description;
                    props[p.name]     = std::move(pj);
                    if (p.required) required_arr.push_back(p.name);
                }
                params_schema["properties"] = std::move(props);
                params_schema["required"]   = std::move(required_arr);
                fn["parameters"]            = std::move(params_schema);
                tj["function"]              = std::move(fn);
                tools_json.push_back(std::move(tj));
            }
            body["tools"] = std::move(tools_json);
        }

        std::string body_str = to_json_string(body);
        std::string response_body;

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
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            resp.finish_reason = FinishReason::Error;
            resp.error_message = std::string("curl error: ") + curl_easy_strerror(res);
            return resp;
        }

        if (stop.stop_requested()) {
            resp.finish_reason = FinishReason::Cancelled;
            return resp;
        }

        auto parsed = parse_json(response_body);
        if (!parsed) {
            resp.finish_reason = FinishReason::Error;
            resp.error_message = "Failed to parse response";
            return resp;
        }

        const auto& r = *parsed;
        if (r.contains("error")) {
            resp.finish_reason = FinishReason::Error;
            resp.error_message = r["error"].value("message", "Unknown API error");
            return resp;
        }

        if (r.contains("usage") && r["usage"].is_object()) {
            resp.input_tokens  = r["usage"].value("prompt_tokens",     0);
            resp.output_tokens = r["usage"].value("completion_tokens", 0);
        }

        if (r.contains("choices") && r["choices"].is_array() && !r["choices"].empty()) {
            const auto& choice = r["choices"][0];
            std::string finish = choice.value("finish_reason", "stop");
            if (finish == "tool_calls") resp.finish_reason = FinishReason::ToolUse;
            else if (finish == "length") resp.finish_reason = FinishReason::MaxTokens;
            else                          resp.finish_reason = FinishReason::Stop;

            if (choice.contains("message") && choice["message"].is_object()) {
                const auto& msg = choice["message"];
                resp.text = msg.value("content", "");
                if (delta_cb && !resp.text.empty()) delta_cb(resp.text);

                if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
                    for (const auto& tc : msg["tool_calls"]) {
                        ModelToolCall call;
                        call.call_id = tc.value("id", "");
                        if (tc.contains("function") && tc["function"].is_object()) {
                            call.name = tc["function"].value("name", "");
                            std::string args_str = tc["function"].value("arguments", "{}");
                            auto parsed_args = parse_json(args_str);
                            call.arguments = parsed_args ? *parsed_args : JsonValue::object();
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
