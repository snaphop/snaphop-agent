#include <catch2/catch_test_macros.hpp>
#include "agent/session_manager.hpp"
#include <filesystem>
#include <thread>
#include <atomic>

namespace fs = std::filesystem;

static agent::Config test_config() {
    agent::Config cfg;
    cfg.runtime.data_dir          = "/tmp/agent_session_tests_" + std::to_string(getpid());
    cfg.runtime.default_workspace = cfg.runtime.data_dir + "/workspace";
    cfg.limits.max_concurrent_sessions = 4;
    return cfg;
}

TEST_CASE("SessionManager: get_or_create returns same session on second call", "[session]") {
    auto cfg = test_config();
    agent::SessionManager mgr(cfg);

    auto s1 = mgr.get_or_create("sess1");
    REQUIRE(s1.has_value());

    auto s2 = mgr.get_or_create("sess1");
    REQUIRE(s2.has_value());

    CHECK(*s1 == *s2); // same pointer

    fs::remove_all(cfg.runtime.data_dir);
}

TEST_CASE("SessionManager: different session IDs are distinct", "[session]") {
    auto cfg = test_config();
    agent::SessionManager mgr(cfg);

    auto s1 = mgr.get_or_create("a");
    auto s2 = mgr.get_or_create("b");
    REQUIRE(s1.has_value());
    REQUIRE(s2.has_value());
    CHECK(*s1 != *s2);

    fs::remove_all(cfg.runtime.data_dir);
}

TEST_CASE("SessionManager: respects max_concurrent_sessions limit", "[session]") {
    auto cfg = test_config();
    cfg.limits.max_concurrent_sessions = 2;
    agent::SessionManager mgr(cfg);

    REQUIRE(mgr.get_or_create("s1").has_value());
    REQUIRE(mgr.get_or_create("s2").has_value());
    auto s3 = mgr.get_or_create("s3");
    REQUIRE(!s3.has_value());
    CHECK(s3.error().message.find("Max concurrent") != std::string::npos);

    fs::remove_all(cfg.runtime.data_dir);
}

TEST_CASE("SessionManager: turn_mutex serializes concurrent turns", "[session]") {
    auto cfg = test_config();
    agent::SessionManager mgr(cfg);

    auto sess = mgr.get_or_create("concurrent");
    REQUIRE(sess.has_value());

    std::atomic<int> concurrent_count{0};
    std::atomic<int> max_concurrent{0};
    constexpr int N = 8;

    auto worker = [&]() {
        std::unique_lock<std::mutex> lock((*sess)->turn_mutex);
        int n = concurrent_count.fetch_add(1, std::memory_order_relaxed) + 1;
        int prev = max_concurrent.load(std::memory_order_relaxed);
        while (prev < n && !max_concurrent.compare_exchange_weak(prev, n)) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        concurrent_count.fetch_sub(1, std::memory_order_relaxed);
    };

    std::vector<std::thread> threads;
    threads.reserve(N);
    for (int i = 0; i < N; ++i) threads.emplace_back(worker);
    for (auto& t : threads) t.join();

    // Mutex ensures only one thread at a time holds it.
    CHECK(max_concurrent.load() == 1);

    fs::remove_all(cfg.runtime.data_dir);
}

TEST_CASE("SessionManager: workspace directory is created", "[session]") {
    auto cfg = test_config();
    agent::SessionManager mgr(cfg);

    auto s = mgr.get_or_create("ws_test");
    REQUIRE(s.has_value());
    CHECK(fs::is_directory((*s)->workspace));

    fs::remove_all(cfg.runtime.data_dir);
}
