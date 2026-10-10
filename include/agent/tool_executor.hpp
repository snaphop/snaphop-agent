#pragma once

#include <cstddef>
#include <functional>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

namespace agent {

// Runtime-owned fixed worker pool, started lazily for read-only tool batches.
// Only the caller publishes events/transcripts. The queue has a fixed capacity.
class ToolExecutor {
  public:
    explicit ToolExecutor(size_t workers);
    ~ToolExecutor();
    std::future<void> submit(std::function<void()> task);
    size_t worker_count() const {
        return worker_count_;
    }

  private:
    void start_locked();
    size_t worker_count_;
    std::mutex mutex_;
    std::condition_variable_any ready_;
    std::condition_variable space_;
    std::deque<std::packaged_task<void()>> queue_;
    std::vector<std::jthread> workers_;
};

} // namespace agent
