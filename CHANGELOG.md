# Changelog

All notable changes to this project are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Four canonical repository review skills with discovery metadata and Claude symlinks,
  plus code-review guidance, CLI/configuration contract, local setup and a VM repository comparison.
- Required Cppcheck static analysis in local and CI verification, with diagnostic logs.
- Configurable concurrent builds and complete-project GCC coverage reporting with
  regression floors; the requested 100% coverage target remains outstanding.
- MIT license and dependency notices, GitHub verification and tag-triggered
  release workflows, grouped Actions dependency updates, and a PR template.
- Local verification and Linux x86_64 release packaging scripts.
- Paginated UTF-8 file reads, useful bounded tool output, and stalled tool-loop detection.
- A bounded worker pool for approved read-only tool batches, configurable with `max_parallel_tools`.
- Interrupted tool-batch recovery with explicit unknown outcomes and unique local execution IDs.
- Local HTTP protocol smoke tests and 1,000-turn performance observations.
- Explicit overwrite approval policy, bounded session caching, and automatic transcript-size compaction.
- Regression tests for provider continuations, cancellation, input validation, prompt bounds, and transcript recovery.

### Changed
- Moved the canonical repository to `snaphop/snaphop-agent` on GitHub, preserving
  Gitea Git history; replaced the placeholder security contact with SnapHop's.
- Cache and freeze tool definitions; share canonical input schemas between validation and provider adapters.
- Reuse stable context within a turn and trim older tool payloads under prompt pressure while preserving call/result pairing.
- Reject undeclared tool arguments and duplicate provider call IDs within a response before execution.
- Updated pinned dependencies to nlohmann/json 3.12.0 and Catch2 3.16.1.
- Test dependencies are optional with `BUILD_TESTING=OFF`.
- Require an explicit model ID; removed the stale implicit Anthropic model fallback.
- CLI stdout now contains the final answer; JSONL diagnostics go to stderr.
- Replaced placeholder architecture/contribution instructions and recorded remaining v0 work.

### Fixed
- Duplicated user/tool history and lost native provider tool-call data.
- Unenforced prompt budgets, optional argument validation, and symlinked-parent escapes.
- Cancellation/iteration-limit success reports, shell group timeout handling, and HTTP response bounds.
- Truncated-tail append recovery, unbounded compaction, and lost cumulative compaction coverage.
- Session ID traversal, configured credential disclosure, and malformed JSONL crashes.

## [0.1.0] - 2026-05-12

### Added
- Full v0 implementation of the snaphop-agent runtime (all 5 milestones).
- CMake build system with C++23, FetchContent for nlohmann/json and Catch2.
- `AgentRuntime` — turn lifecycle hub with tool-call continuation loop (max 16 iterations).
- `SessionManager` — session creation, workspace provisioning, per-session turn mutex.
- `TranscriptStore` — append-only JSONL log with byte-budgeted `load_recent()`, atomic `compact()`.
- `PromptBuilder` — byte-budgeted prompt construction from transcript history and memory summary.
- `IModelClient` interface with `AnthropicClient` (Anthropic Messages API) and `OpenAIClient` (OpenAI-compatible) adapters via libcurl.
- `ToolRegistry` and `ITool` interface with typed parameter validation.
- `ToolPolicy` — allow/deny lists, dangerous-tool flag enforcement.
- `resolve_workspace_path()` — centralized path safety: rejects absolute paths, traversal, symlinks outside workspace, and NUL bytes.
- Built-in tools: `echo`, `list_dir`, `read_file`, `write_file`, `shell` (disabled by default).
- `MemoryManager` — file-backed summary and key-value notes per session.
- `agentd` — headless JSONL stdin/stdout daemon.
- `agent_cli` — interactive CLI for manual testing.
- 38 unit and integration tests covering all required safety and lifecycle scenarios.
- `examples/config.example.json` and `examples/stdin_session.example.jsonl`.
