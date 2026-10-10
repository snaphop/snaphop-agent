#include "agent/tool.hpp"
#include "agent/config.hpp"
#include "agent/tool_result_budget.hpp"
#include <algorithm>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <poll.h>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <cerrno>

namespace agent {

namespace {

// Keep early context and late diagnostics with constant storage, even for a
// command that keeps writing after the output cap has been reached.
class OutputCapture {
  public:
    explicit OutputCapture(size_t limit)
        : limit_(limit), head_limit_(limit - limit / 4), tail_(limit / 4, '\0') {}

    void append(std::string_view bytes) {
        if (bytes.size() > limit_ - std::min(total_, limit_))
            truncated_ = true;
        total_ = std::min(limit_, total_ + std::min(bytes.size(), limit_));
        const size_t take = std::min(bytes.size(), head_limit_ - head_.size());
        head_.append(bytes.substr(0, take));
        bytes.remove_prefix(take);
        if (tail_.empty() || bytes.empty())
            return;
        if (bytes.size() >= tail_.size()) {
            std::copy(bytes.end() - static_cast<std::ptrdiff_t>(tail_.size()), bytes.end(),
                      tail_.begin());
            cursor_ = 0;
            tail_size_ = tail_.size();
        } else {
            const size_t first = std::min(bytes.size(), tail_.size() - cursor_);
            std::copy_n(bytes.begin(), first, tail_.begin() + static_cast<std::ptrdiff_t>(cursor_));
            std::copy(bytes.begin() + static_cast<std::ptrdiff_t>(first), bytes.end(),
                      tail_.begin());
            cursor_ = (cursor_ + bytes.size()) % tail_.size();
            tail_size_ = std::min(tail_.size(), tail_size_ + bytes.size());
        }
    }

    std::string text() const {
        const auto tail = tail_size_ < tail_.size()
                              ? tail_.substr(0, tail_size_)
                              : tail_.substr(cursor_) + tail_.substr(0, cursor_);
        constexpr std::string_view marker = "\n[truncated]\n";
        if (!truncated_)
            return head_ + tail;
        if (limit_ < marker.size())
            return utf8_prefix(head_ + tail, limit_);
        const size_t available = limit_ - marker.size();
        size_t start = tail.size() - std::min(tail.size(), available);
        while (start < tail.size() && (static_cast<unsigned char>(tail[start]) & 0xc0) == 0x80)
            ++start;
        const auto safe_tail = tail.substr(start);
        return utf8_prefix(head_, available - safe_tail.size()) + std::string(marker) + safe_tail;
    }

    bool truncated() const {
        return truncated_;
    }

  private:
    size_t limit_, head_limit_, total_ = 0;
    std::string head_, tail_;
    size_t cursor_ = 0, tail_size_ = 0;
    bool truncated_ = false;
};

} // namespace

// Exec-style command runner; arguments are passed directly to execvp.
class ShellTool : public ITool {
  public:
    ToolDefinition definition() const override {
        return {
            .name = "shell",
            .description = "Execute a command in the workspace. Disabled by default. "
                           "No shell interpolation; provide argv array.",
            .params =
                {
                    {.name = "command",
                     .type = "string",
                     .required = true,
                     .description = "Command to run (first token is the executable)"},
                    {.name = "args",
                     .type = "array",
                     .required = false,
                     .description = "Additional arguments (preferred over embedding in command)",
                     .item_type = "string"},
                    {.name = "timeout_seconds",
                     .type = "integer",
                     .required = false,
                     .description = "Per-call timeout override (capped by global limit)",
                     .minimum = 1},
                },
            .dangerous = true,
        };
    }

    ToolResult call(const ToolCall& tc, const ToolContext& ctx) override {
        std::string command = tc.arguments.value("command", "");
        if (command.empty()) {
            return {.call_id = tc.call_id,
                    .ok = false,
                    .content = nullptr,
                    .error = "command is required"};
        }

        uint32_t max_secs = ctx.limits ? ctx.limits->max_tool_seconds : 30u;
        if (tc.arguments.contains("timeout_seconds") &&
            tc.arguments["timeout_seconds"].is_number_integer()) {
            if (tc.arguments["timeout_seconds"].get<int64_t>() <= 0) {
                return {.call_id = tc.call_id,
                        .ok = false,
                        .content = nullptr,
                        .error = "timeout_seconds must be positive"};
            }
            uint32_t req = tc.arguments["timeout_seconds"].get<uint32_t>();
            max_secs = std::min(max_secs, req);
        }
        size_t max_output = ctx.limits ? ctx.limits->max_shell_output_bytes : 65536u;

        // Build argv. command is the executable; optional "args" list appended.
        std::vector<std::string> argv_strings;
        argv_strings.push_back(command);
        if (tc.arguments.contains("args") && tc.arguments["args"].is_array()) {
            for (const auto& a : tc.arguments["args"]) {
                if (!a.is_string())
                    return {.call_id = tc.call_id,
                            .ok = false,
                            .content = nullptr,
                            .error = "args must contain strings"};
                argv_strings.push_back(a.get<std::string>());
            }
        }
        for (const auto& arg : argv_strings) {
            if (arg.find('\0') != std::string::npos)
                return {.call_id = tc.call_id,
                        .ok = false,
                        .content = nullptr,
                        .error = "NUL in command arguments"};
        }
        std::vector<char*> argv;
        for (auto& s : argv_strings)
            argv.push_back(s.data());
        argv.push_back(nullptr);

        // Pipe for stdout+stderr combined.
        int pipefd[2];
        if (pipe2(pipefd, O_CLOEXEC) != 0) {
            return {
                .call_id = tc.call_id, .ok = false, .content = nullptr, .error = "pipe() failed"};
        }

        pid_t pid = fork();
        if (pid < 0) {
            close(pipefd[0]);
            close(pipefd[1]);
            return {
                .call_id = tc.call_id, .ok = false, .content = nullptr, .error = "fork() failed"};
        }

        if (pid == 0) {
            // Child
            if (setpgid(0, 0) != 0)
                _exit(126);
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[0]);
            close(pipefd[1]);

            // Change to workspace directory.
            if (chdir(ctx.workspace.c_str()) != 0)
                _exit(126);

            execvp(argv[0], argv.data());
            _exit(127);
        }

        // Parent also sets the group to close the fork scheduling race.
        setpgid(pid, pid);
        // Parent: read output with timeout.
        close(pipefd[1]);

        OutputCapture output(max_output);
        bool timed_out = false;

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(max_secs);

        while (true) {
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline || ctx.stop.stop_requested()) {
                timed_out = true;
                // Kill process group.
                kill(-pid, SIGKILL);
                kill(pid, SIGKILL);
                break;
            }

            auto ms_left =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();

            struct pollfd pfd = {.fd = pipefd[0], .events = POLLIN, .revents = 0};
            int poll_ms = static_cast<int>(ms_left > 500 ? 500 : ms_left);
            int ret = poll(&pfd, 1, poll_ms);
            if (ret < 0)
                break;
            if (ret == 0)
                continue; // timeout poll, loop to check deadline

            if (pfd.revents & POLLIN) {
                char buf[4096];
                ssize_t n = read(pipefd[0], buf, sizeof(buf));
                if (n <= 0)
                    break;
                output.append(std::string_view(buf, static_cast<size_t>(n)));
            }
            if (!(pfd.revents & POLLIN) && (pfd.revents & (POLLHUP | POLLERR)))
                break;
        }

        close(pipefd[0]);

        int status = 0;
        while (waitpid(pid, &status, WNOHANG) == 0) {
            if (std::chrono::steady_clock::now() >= deadline || ctx.stop.stop_requested()) {
                timed_out = true;
                kill(-pid, SIGKILL);
                kill(pid, SIGKILL);
                while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
                }
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

        JsonValue result;
        result["output"] = output.text();
        result["exit_code"] = exit_code;
        result["truncated"] = output.truncated();
        result["timed_out"] = timed_out;

        bool ok = (exit_code == 0 && !timed_out);
        std::string err;
        if (timed_out)
            err = "Command timed out after " + std::to_string(max_secs) + " seconds";
        else if (exit_code != 0)
            err = "Exit code " + std::to_string(exit_code);

        return {.call_id = tc.call_id, .ok = ok, .content = result, .error = err};
    }
};

std::unique_ptr<ITool> make_shell_tool() {
    return std::make_unique<ShellTool>();
}

} // namespace agent
