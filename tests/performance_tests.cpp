#include <catch2/catch_test_macros.hpp>
#include "agent/agent_runtime.hpp"
#include <fstream>
#include <iostream>
#include <unistd.h>

class RepeatingModel final : public agent::IModelClient {
  public:
    size_t largest_request = 0;
    agent::ModelResponse complete(const agent::ModelRequest& req, std::stop_token,
                                  agent::DeltaCallback) override {
        largest_request = std::max(
            largest_request, agent::to_json_string(agent::anthropic_request_body(req)).size());
        agent::ModelResponse response;
        response.text = "ok";
        return response;
    }
};

static size_t resident_bytes() {
    std::ifstream stat("/proc/self/statm");
    size_t total = 0, resident = 0;
    stat >> total >> resident;
    return resident * static_cast<size_t>(sysconf(_SC_PAGESIZE));
}

TEST_CASE("Performance: 1000 mock turns keep context bounded", "[performance]") {
    agent::Config cfg;
    cfg.runtime.data_dir = "/tmp/agent_perf_" + std::to_string(getpid());
    cfg.runtime.default_workspace = cfg.runtime.data_dir + "/workspace";
    cfg.limits.max_prompt_bytes = 4096;
    auto model = std::make_unique<RepeatingModel>();
    auto* observed = model.get();
    agent::AgentRuntime runtime(cfg, std::move(model), std::make_unique<agent::ToolRegistry>(),
                                std::make_unique<agent::ToolPolicy>(cfg.tools), nullptr);
    std::ofstream sink("/dev/null");
    agent::set_event_stream(sink);
    struct ResetStream {
        ~ResetStream() {
            agent::set_event_stream(std::cout);
        }
    } reset_stream;
    size_t baseline = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i) {
        REQUIRE(runtime.run_turn({"perf", "hello", ""}));
        if (i == 99)
            baseline = resident_bytes();
    }
    const auto elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    const auto final_rss = resident_bytes();
    WARN("1000 turns: " << elapsed << " ms; mean " << elapsed / 1000 << " ms; RSS after warmup "
                        << baseline << " bytes; final " << final_rss << " bytes");
    CHECK(observed->largest_request <= cfg.limits.max_prompt_bytes);
    CHECK(runtime.sessions().active_count() == 1);
    if (final_rss > baseline + 16 * 1024 * 1024)
        WARN("RSS growth exceeded the 16 MiB observation threshold");
    std::filesystem::remove_all(cfg.runtime.data_dir);
}
