#include "agent/agent_runtime.hpp"
#include "agent/config.hpp"
#include "agent/event.hpp"
#include <iostream>
#include <string>
#include <cstring>
#include <filesystem>

static void print_usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " [options] \"message\"\n"
              << "\n"
              << "Options:\n"
              << "  --config <path>     Path to agent.json (default: agent.json)\n"
              << "  --session <id>      Session ID (default: default)\n"
              << "  --workspace <path>  Workspace directory (default from config)\n"
              << "  --help              Show this help\n"
              << "\n"
              << "The assistant's final response is written to stdout.\n"
              << "JSONL diagnostics go to stderr.\n";
}

int main(int argc, char* argv[]) {
    std::string config_path = "agent.json";
    std::string session_id = "default";
    std::string workspace;
    std::string message;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--session" && i + 1 < argc) {
            session_id = argv[++i];
        } else if (arg == "--workspace" && i + 1 < argc) {
            workspace = argv[++i];
        } else if (!arg.starts_with("--") && message.empty()) {
            message = arg;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (message.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    // Load config (tolerate missing file with defaults).
    agent::Config cfg;
    if (std::filesystem::exists(config_path)) {
        auto loaded = agent::load_config(config_path);
        if (!loaded) {
            std::cerr << "Config error: " << loaded.error() << "\n";
            return 1;
        }
        cfg = std::move(*loaded);
    }
    agent::apply_env_overrides(cfg);

    agent::set_event_stream(std::cerr);
    // Build runtime.
    auto runtime = agent::make_default_runtime(std::move(cfg));

    agent::TurnInput input{
        .session_id = session_id,
        .message = message,
        .workspace = workspace,
    };

    auto result = runtime->run_turn(input);

    if (!result) {
        std::cerr << "Error: " << result.error().message << "\n";
        return 1;
    }

    std::cout << result->final_text << "\n";

    return 0;
}
