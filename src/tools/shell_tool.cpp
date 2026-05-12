#include "agent/tool.hpp"
#include "agent/config.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <poll.h>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>

namespace agent {

// Exec-style shell tool. No shell interpolation: argv is split on spaces naively
// for safety. A proper implementation would accept an args array.
class ShellTool : public ITool {
public:
    ToolDefinition definition() const override {
        return {
            .name = "shell",
            .description = "Execute a command in the workspace. Disabled by default. "
                           "No shell interpolation; provide argv array.",
            .params = {
                {.name = "command", .type = "string", .required = true,
                 .description = "Command to run (first token is the executable)"},
                {.name = "args", .type = "array", .required = false,
                 .description = "Additional arguments (preferred over embedding in command)"},
                {.name = "timeout_seconds", .type = "integer", .required = false,
                 .description = "Per-call timeout override (capped by global limit)"},
            },
            .dangerous = true,
        };
    }

    ToolResult call(const ToolCall& tc, const ToolContext& ctx) override {
        std::string command = tc.arguments.value("command", "");
        if (command.empty()) {
            return {.call_id = tc.call_id, .ok = false,
                    .content = nullptr, .error = "command is required"};
        }

        uint32_t max_secs = ctx.limits ? ctx.limits->max_tool_seconds : 30u;
        if (tc.arguments.contains("timeout_seconds") && tc.arguments["timeout_seconds"].is_number_integer()) {
            uint32_t req = tc.arguments["timeout_seconds"].get<uint32_t>();
            max_secs = std::min(max_secs, req);
        }
        size_t max_output = ctx.limits ? ctx.limits->max_shell_output_bytes : 65536u;

        // Build argv. command is the executable; optional "args" list appended.
        std::vector<std::string> argv_strings;
        argv_strings.push_back(command);
        if (tc.arguments.contains("args") && tc.arguments["args"].is_array()) {
            for (const auto& a : tc.arguments["args"]) {
                if (a.is_string()) argv_strings.push_back(a.get<std::string>());
            }
        }
        std::vector<char*> argv;
        for (auto& s : argv_strings) argv.push_back(s.data());
        argv.push_back(nullptr);

        // Pipe for stdout+stderr combined.
        int pipefd[2];
        if (pipe2(pipefd, O_CLOEXEC) != 0) {
            return {.call_id = tc.call_id, .ok = false,
                    .content = nullptr, .error = "pipe() failed"};
        }

        pid_t pid = fork();
        if (pid < 0) {
            close(pipefd[0]); close(pipefd[1]);
            return {.call_id = tc.call_id, .ok = false,
                    .content = nullptr, .error = "fork() failed"};
        }

        if (pid == 0) {
            // Child
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[0]); close(pipefd[1]);

            // Change to workspace directory.
            if (chdir(ctx.workspace.c_str()) != 0) _exit(126);

            execvp(argv[0], argv.data());
            _exit(127);
        }

        // Parent: read output with timeout.
        close(pipefd[1]);

        std::string output;
        bool truncated = false;
        bool timed_out = false;

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(max_secs);

        while (true) {
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                timed_out = true;
                // Kill process group.
                kill(-pid, SIGKILL);
                kill(pid, SIGKILL);
                break;
            }

            auto ms_left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();

            struct pollfd pfd = {.fd = pipefd[0], .events = POLLIN, .revents = 0};
            int poll_ms = static_cast<int>(ms_left > 500 ? 500 : ms_left);
            int ret = poll(&pfd, 1, poll_ms);
            if (ret < 0) break;
            if (ret == 0) continue; // timeout poll, loop to check deadline

            if (pfd.revents & POLLIN) {
                char buf[4096];
                ssize_t n = read(pipefd[0], buf, sizeof(buf));
                if (n <= 0) break;
                size_t remaining = max_output - output.size();
                if (remaining == 0) { truncated = true; continue; }
                size_t take = std::min(static_cast<size_t>(n), remaining);
                output.append(buf, take);
                if (take < static_cast<size_t>(n)) truncated = true;
            }
            if (pfd.revents & (POLLHUP | POLLERR)) break;
        }

        close(pipefd[0]);

        int status = 0;
        waitpid(pid, &status, 0);
        int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

        JsonValue result;
        result["output"]    = output;
        result["exit_code"] = exit_code;
        result["truncated"] = truncated;
        result["timed_out"] = timed_out;

        bool ok = (exit_code == 0 && !timed_out);
        std::string err;
        if (timed_out) err = "Command timed out after " + std::to_string(max_secs) + " seconds";
        else if (exit_code != 0) err = "Exit code " + std::to_string(exit_code);

        return {.call_id = tc.call_id, .ok = ok, .content = result, .error = err};
    }
};

std::unique_ptr<ITool> make_shell_tool() {
    return std::make_unique<ShellTool>();
}

} // namespace agent
