#include "agent/agent_runtime.hpp"
#include "agent/config.hpp"
#include "agent/event.hpp"
#include "agent/json.hpp"
#include <iostream>
#include <string>
#include <filesystem>
#include <csignal>
#include <atomic>

static std::atomic<bool> g_shutdown{false};

static void handle_signal(int) {
    g_shutdown.store(true, std::memory_order_relaxed);
}

static void print_usage(const char* argv0) {
    std::cerr
        << "Usage: " << argv0 << " [options]\n"
        << "\n"
        << "Reads JSON envelopes from stdin (one per line) and emits JSONL events to stdout.\n"
        << "\n"
        << "Input envelope:\n"
        << "  "
           "{\"type\":\"user_message\",\"session_id\":\"...\",\"message\":\"...\",\"workspace\":\"."
           "..\"}\n"
        << "\n"
        << "Options:\n"
        << "  --config <path>   Path to agent.json (default: agent.json)\n"
        << "  --help            Show this help\n";
}

int main(int argc, char* argv[]) {
    std::string config_path = "agent.json";

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

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

    auto runtime = agent::make_default_runtime(std::move(cfg));

    std::string line;
    while (!g_shutdown.load(std::memory_order_relaxed) && std::getline(std::cin, line)) {
        if (line.empty())
            continue;

        auto parsed = agent::parse_json(line);
        if (!parsed) {
            agent::emit_event(agent::make_error_event("Invalid JSON input"));
            continue;
        }

        const auto& env = *parsed;
        if (!env.is_object()) {
            agent::emit_event(agent::make_error_event("Input envelope must be an object"));
            continue;
        }
        bool valid = true;
        for (const auto* key : {"type", "session_id", "message", "workspace"}) {
            if (env.contains(key) && !env[key].is_string())
                valid = false;
        }
        if (!valid) {
            agent::emit_event(agent::make_error_event("Envelope fields must be strings"));
            continue;
        }
        std::string type = env.value("type", "");
        if (type != "user_message") {
            agent::emit_event(agent::make_error_event("Unknown envelope type: " + type));
            continue;
        }

        agent::TurnInput input;
        input.session_id = env.value("session_id", "default");
        input.message = env.value("message", "");
        input.workspace = env.value("workspace", "");

        if (input.message.empty()) {
            agent::emit_event(
                agent::make_error_event("message field is required", input.session_id));
            continue;
        }

        std::stop_source stop_src;
        auto result = runtime->run_turn(input, stop_src.get_token());
        if (!result) {
            agent::emit_event(agent::make_error_event(result.error().message, input.session_id));
        }
    }

    return 0;
}
