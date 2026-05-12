#include "agent/tool.hpp"

namespace agent {

class EchoTool : public ITool {
public:
    ToolDefinition definition() const override {
        return {
            .name = "echo",
            .description = "Return the input text unchanged. Useful for testing.",
            .params = {
                {.name = "text", .type = "string", .required = true,
                 .description = "Text to echo back"}
            },
            .dangerous = false,
        };
    }

    ToolResult call(const ToolCall& tc, const ToolContext&) override {
        std::string text = tc.arguments.value("text", "");
        return {.call_id = tc.call_id, .ok = true, .content = text, .error = ""};
    }
};

std::unique_ptr<ITool> make_echo_tool() {
    return std::make_unique<EchoTool>();
}

} // namespace agent
