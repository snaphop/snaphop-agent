#pragma once

#include "tool.hpp"
#include <array>
#include <cstdint>

namespace agent {

// Bounded, turn-local progress heuristic. Changing outcomes break a repeat streak.
class ToolLoopGuard {
  public:
    enum class Decision { Continue, Warn, Stop };
    Decision observe(const ToolCall& call, const ToolResult& result, bool read_only);
    static constexpr size_t max_calls = 128;

  private:
    struct Digest {
        uint64_t hash = 0;
        size_t bytes = 0;
        bool operator==(const Digest&) const = default;
    };
    struct Record {
        Digest action;
        Digest outcome;
        bool eligible = false;
        bool progress = true;
    };
    static Digest digest(std::string_view value);
    std::array<Record, 32> history_{};
    size_t count_ = 0;
};

} // namespace agent
