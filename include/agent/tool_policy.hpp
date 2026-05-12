#pragma once

#include "config.hpp"
#include "tool.hpp"
#include <string>

namespace agent {

enum class PolicyDecision {
    Allow,
    Deny,
};

struct PolicyViolation {
    std::string reason;
};

// Enforces allow/deny lists and dangerous-tool policy.
// Policy order: explicit deny > unknown tool denied > dangerous requires flag > allow.
class ToolPolicy {
public:
    explicit ToolPolicy(ToolsConfig cfg);

    // Check whether a tool call is permitted before execution.
    std::expected<PolicyDecision, PolicyViolation>
    check(const ToolDefinition& def) const;

    bool is_allowed(const ToolDefinition& def) const;

private:
    ToolsConfig cfg_;
};

} // namespace agent
