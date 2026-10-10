#include "agent/tool_executor.hpp"
#include <algorithm>

namespace agent {

ToolExecutor::ToolExecutor(size_t workers) : worker_count_(std::clamp<size_t>(workers, 1, 32)) {}

ToolExecutor::~ToolExecutor() {
    for (auto& worker : workers_)
        worker.request_stop();
    ready_.notify_all();
    // Join while queue/mutex/condition variables are still alive.
    workers_.clear();
}

void ToolExecutor::start_locked() {
    if (!workers_.empty())
        return;
    workers_.reserve(worker_count_);
    for (size_t i = 0; i < worker_count_; ++i) {
        workers_.emplace_back([this](std::stop_token stop) {
            while (true) {
                std::packaged_task<void()> task;
                {
                    std::unique_lock lock(mutex_);
                    ready_.wait(lock, stop, [&] { return !queue_.empty(); });
                    if (queue_.empty())
                        return;
                    task = std::move(queue_.front());
                    queue_.pop_front();
                }
                space_.notify_one();
                task();
            }
        });
    }
}

std::future<void> ToolExecutor::submit(std::function<void()> function) {
    std::packaged_task<void()> task(std::move(function));
    auto result = task.get_future();
    {
        std::unique_lock lock(mutex_);
        start_locked();
        space_.wait(lock, [&] { return queue_.size() < 32; });
        queue_.push_back(std::move(task));
    }
    ready_.notify_one();
    return result;
}

} // namespace agent
