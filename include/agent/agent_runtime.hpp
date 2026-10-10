#pragma once

#include "config.hpp"
#include "session_manager.hpp"
#include "model_client.hpp"
#include "tool.hpp"
#include "tool_policy.hpp"
#include "prompt_builder.hpp"
#include "memory_manager.hpp"
#include "event.hpp"
#include <string>
#include <memory>
#include <stop_token>
#include <expected>
#include <memory_resource>

namespace agent {

struct RuntimeError {
    std::string message;
    bool retryable = false;
};

struct TurnInput {
    std::string session_id;
    std::string message;
    std::string workspace; // may be empty; falls back to config default
};

struct TurnOutput {
    std::string session_id;
    std::string turn_id;
    std::string final_text;
    bool had_tool_calls = false;
    int tool_call_count = 0;
};

// The main agent turn-processing hub.
// Owns session management, tool registry, model client, and orchestrates a
// full user turn: input -> model -> tools -> output.
class AgentRuntime {
  public:
    AgentRuntime(Config cfg, std::unique_ptr<IModelClient> model_client,
                 std::unique_ptr<ToolRegistry> tools, std::unique_ptr<ToolPolicy> policy,
                 std::unique_ptr<MemoryManager> memory);

    // Process one user turn. May invoke the model multiple times if the model
    // requests tool calls. Emits JSONL events via emit_event().
    std::expected<TurnOutput, RuntimeError> run_turn(const TurnInput& input,
                                                     std::stop_token stop = {});

    const Config& config() const {
        return cfg_;
    }
    SessionManager& sessions() {
        return sessions_;
    }

  private:
    void emit(const JsonValue& event) const;

    // Unique turn ID generator.
    std::string generate_turn_id() const;
    std::string generate_call_id() const;

    // Single iteration of the model loop.
    // Returns true if the model requested more tool calls (loop continues).
    std::expected<bool, RuntimeError>
    model_iteration(SessionState& session, const std::string& turn_id,
                    std::pmr::vector<JsonValue>& transcript_scratch, std::string& final_text_out,
                    int& tool_call_count, std::stop_token stop);

    std::mutex active_mutex_;
    size_t active_turns_ = 0;
    Config cfg_;
    SessionManager sessions_;
    PromptBuilder prompt_builder_;
    std::unique_ptr<MemoryManager> memory_;
    std::unique_ptr<IModelClient> model_client_;
    std::unique_ptr<ToolRegistry> tools_;
    std::unique_ptr<ToolPolicy> policy_;
};

// Build a default runtime from config (registers built-in tools, resolves env vars).
std::unique_ptr<AgentRuntime> make_default_runtime(Config cfg);

} // namespace agent
