#include "agent/tool_loop_guard.hpp"
#include <algorithm>

namespace agent {

ToolLoopGuard::Digest ToolLoopGuard::digest(std::string_view value) {
    // Fingerprints are liveness hints, not an authorization or replay identity.
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char byte : value) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return {hash, value.size()};
}

ToolLoopGuard::Decision ToolLoopGuard::observe(const ToolCall& call, const ToolResult& result,
                                               bool read_only) {
    const auto action =
        digest(to_json_string(JsonValue{{"tool", call.name}, {"arguments", call.arguments}}));
    const auto outcome = digest(to_json_string(
        JsonValue{{"ok", result.ok}, {"content", result.content}, {"error", result.error}}));
    const bool eligible = !result.ok || read_only;
    bool progress = true;
    const size_t available = std::min(count_, history_.size());
    if (eligible) {
        for (size_t n = 0; n < available; ++n) {
            const auto& previous = history_[(count_ - n - 1) % history_.size()];
            if (previous.action == action) {
                progress = !previous.eligible || previous.outcome != outcome;
                break;
            }
        }
    }
    size_t repeats = 1;
    if (eligible && !progress) {
        for (size_t n = 0; n < available; ++n) {
            const auto& previous = history_[(count_ - n - 1) % history_.size()];
            if (previous.action == action) {
                if (!previous.eligible || previous.outcome != outcome)
                    break;
                ++repeats;
            }
            // A new action, changed observation, or successful mutation is a possible
            // progress signal. Don't punish a repeated read amid productive work.
            if (previous.progress)
                break;
        }
    }
    history_[count_++ % history_.size()] = {action, outcome, eligible, progress};
    if (repeats >= 5)
        return Decision::Stop;
    return repeats == 3 ? Decision::Warn : Decision::Continue;
}

} // namespace agent
