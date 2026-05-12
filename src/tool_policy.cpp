#include "agent/tool_policy.hpp"
#include <algorithm>

namespace agent {

ToolPolicy::ToolPolicy(ToolsConfig cfg) : cfg_(std::move(cfg)) {}

std::expected<PolicyDecision, PolicyViolation>
ToolPolicy::check(const ToolDefinition& def) const {
    const auto& name = def.name;

    // 1. Explicit deny wins.
    if (std::find(cfg_.deny.begin(), cfg_.deny.end(), name) != cfg_.deny.end())
        return std::unexpected(PolicyViolation{"Tool '" + name + "' is explicitly denied"});

    // 2. Unknown tool is denied (not on the allow list).
    bool allowed = std::find(cfg_.allow.begin(), cfg_.allow.end(), name) != cfg_.allow.end();
    if (!allowed)
        return std::unexpected(PolicyViolation{"Tool '" + name + "' is not on the allow list"});

    // 3. Dangerous tools require explicit enable flag.
    if (def.dangerous && !cfg_.enable_dangerous)
        return std::unexpected(PolicyViolation{
            "Tool '" + name + "' is dangerous and enable_dangerous is false"});

    return PolicyDecision::Allow;
}

bool ToolPolicy::is_allowed(const ToolDefinition& def) const {
    return check(def).has_value();
}

} // namespace agent
