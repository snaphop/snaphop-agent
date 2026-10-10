# AGENTS.md

Canonical contributor and AI-agent instructions. `CLAUDE.md` and `GEMINI.md`
defer to this file.

## Project brief

The product name is **SnapHop Agent**. Use `snaphop-agent` for the repository
slug and release archive filenames, and `snaphop_agent` for the CMake identifier.

Build a fast, headless, local-first AI agent in modern C++ inspired by OpenClaw’s gateway/session/tool-loop architecture, but intentionally smaller and framework-free for the initial release.

The first release is **not** a messaging-platform gateway. It is a headless agent runtime that can be driven from a terminal, JSONL stdin/stdout, or a minimal local IPC boundary. Focus on:

1. **Speed**: low startup overhead, low per-turn latency, bounded allocations, efficient transcript loading.
2. **Memory management**: predictable ownership, bounded context, compact transcripts, safe workspace access.
3. **Tool calling**: typed tool discovery, validation, execution, event streaming, policy checks, and replayable transcripts.

Do not build GUI, browser dashboard, Discord/Slack/Telegram integrations, plugin marketplace, remote multi-user auth, vector DB memory, or complex distributed execution in the initial release.

---

## Build and test

The canonical repository is https://github.com/snaphop/snaphop-agent.
Run `scripts/check.sh` before handing off a change; GitHub Actions uses the
same script on pull requests and pushes to `master`. Version tags run those
checks and publish Linux x86_64 archives with `LICENSE` and `NOTICE`.
`VERSION=v0.1.0 scripts/build-release.sh` builds locally without publishing.
Keep third-party notices current, and report suspected vulnerabilities only
by email to security@snaphop.com.

Prerequisites: `cmake` ≥ 3.20, GCC ≥ 13 or Clang ≥ 16 (C++23), `libcurl` dev headers.
Full local checks also require Python 3, Cppcheck 2.13+, and Trivy 0.75.0.
Install the checksum-pinned scanner with `scripts/install-trivy.sh`. Security
checks inventory CMake pins and linked Debian/Ubuntu runtime packages, and run
repository vulnerability, secret, misconfiguration, and full license scans.
All security severities gate verification, including unfixed findings. License
entries are informational inventory. Do not suppress findings to make a gate pass.
Private reports are under `<build>/security/`; never upload them to public
issues, PRs, logs, or artifacts. Follow `SECURITY.md` for suspected vulnerabilities.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run all tests
build/agent_tests

# CLI help
build/agent_cli --help

# JSONL daemon (one turn)
printf '%s\n' '{"type":"user_message","session_id":"s1","message":"hello"}' \
  | AGENT_API_KEY=<key> build/agentd --config examples/config.example.json
```

FetchContent automatically downloads `nlohmann/json` (v3.12.0) and `Catch2` (v3.16.1) on first build. No other runtime dependencies required beyond CMake and libcurl.
Catch2 is fetched only with `BUILD_TESTING=ON`. Python 3 is optional developer
tooling for `scripts/check_protocol.py`, not a runtime dependency.
Coverage reporting additionally uses optional Python developer tooling pinned in
`scripts/requirements-coverage.txt`. Run `scripts/coverage.sh` with GCC and a
matching gcov; it instruments all project C++ sources and headers, runs the
regression and loopback HTTP suites, and writes reports under
`build-coverage/coverage/`. The current regression floors are 92% lines and 80%
branches; 100% coverage remains an acceptance gap. Override the floors with
`COVERAGE_MIN_LINES` and `COVERAGE_MIN_BRANCHES` to enforce the full target.
Build scripts use `BUILD_JOBS`, then `CMAKE_BUILD_PARALLEL_LEVEL`, then the local
CPU count. Set `BUILD_JOBS=2` on memory-constrained machines (as in CI).
Local verification additionally requires Cppcheck 2.13+ as developer tooling.
`scripts/check.sh` runs `scripts/static-analysis.sh` after tests; the same gate
runs on pull requests, pushes, and version tags. Standalone analysis configures
CMake with a compile database and analyzes runtime and test translation units,
including project headers, using the build concurrency setting. Reports and
incremental analysis data live under `build/static-analysis/` (or the selected
build directory). Override the tool executable with `CPPCHECK` when needed.
Errors, warnings, performance and portability diagnostics fail verification.
Dependency diagnostics under `_deps` are excluded; analyzer-only Catch2 macro
definitions preserve test expressions. Keep project parsing failures visible,
and document any future narrowly scoped suppression with its justification.

The October 2026 maintenance findings and remaining v0 acceptance gaps are
tracked in `docs/status.md`. Existing milestone checkmarks indicate module
presence, not completion of every listed acceptance criterion.

---

## North star architecture

Use a compact hub-and-spoke design:

```text
                 +----------------------+
stdin / IPC ---> | AgentRuntime / Hub   | ---> stdout events
                 +----------+-----------+
                            |
          +-----------------+-----------------+
          |                 |                 |
     SessionStore       ModelClient       ToolRegistry
          |                 |                 |
     Transcript        Provider API       Built-in tools
     Compaction        Streaming          Shell / FS / HTTP
```

### Core components

- **AgentRuntime**: owns the turn lifecycle. It receives a user message, resolves a session, builds prompt context, calls the model, dispatches tool calls, persists events, and emits final output.
- **SessionManager**: maps session ids to workspace paths, transcript files, locks, and runtime state.
- **TranscriptStore**: append-only JSONL event log. It must support fast append, partial recovery, and bounded replay.
- **PromptBuilder**: builds model input from system prompt, recent transcript, memory summary, tool definitions, and user message.
- **ModelClient**: provider-agnostic interface for chat/completion APIs. The initial implementation may use a single HTTP provider adapter, but keep the interface clean.
- **ToolRegistry**: typed registry of callable tools. Provides tool definitions to the model and executes approved calls.
- **ToolPolicy**: allowlist/denylist, workspace-boundary checks, timeout limits, and confirmation policy.
- **EventBus**: emits lifecycle, assistant, tool, error, and final events as JSONL to stdout or a sink.
- **MemoryManager**: initial version is file-backed only: summary file, transcript compaction, and optional key-value notes. Do not implement vector memory in v0.

---

## Language and dependency policy

Use **C++20 or C++23**.

Prefer standard library facilities:

- `std::string_view`, `std::span`, `std::optional`, `std::variant`, `std::expected` if C++23 is available.
- `std::filesystem` for workspace paths.
- `std::jthread`, `std::stop_token`, `std::mutex`, `std::condition_variable` for concurrency.
- `std::pmr` for hot-path temporary allocation and turn-scoped arenas.

Avoid application frameworks.

Allowed small dependencies, only when justified:

- JSON parser: prefer a fast, well-maintained single-purpose library. Keep it behind `JsonValue` / `JsonParser` wrappers so it can be replaced.
- HTTP client: keep behind `HttpClient`. `libcurl` is acceptable as an optional transport dependency.
- TLS: do not hand-roll crypto or TLS.
- Test framework: acceptable, but keep tests buildable from CMake with one command.

Disallowed in v0:

- Heavy web frameworks.
- ORM layers.
- Plugin frameworks.
- Embedded scripting runtimes.
- Vector databases.
- Global service locators.
- Unbounded background threads.
- Hidden network calls outside `ModelClient` or explicit tools.

---

## Build system

Use CMake.

Required targets:

```text
agentd          # headless daemon / JSONL runtime
agent_cli       # thin CLI wrapper for local manual testing
agent_tests     # unit and integration tests
```

Suggested repository layout:

```text
.
├── AGENTS.md
├── CMakeLists.txt
├── include/
│   └── agent/
│       ├── agent_runtime.hpp
│       ├── config.hpp
│       ├── event.hpp
│       ├── json.hpp
│       ├── memory_manager.hpp
│       ├── model_client.hpp
│       ├── prompt_builder.hpp
│       ├── session_manager.hpp
│       ├── tool.hpp
│       ├── tool_policy.hpp
│       └── transcript_store.hpp
├── src/
│   ├── main_agentd.cpp
│   ├── main_agent_cli.cpp
│   ├── agent_runtime.cpp
│   ├── config.cpp
│   ├── event.cpp
│   ├── json.cpp
│   ├── memory_manager.cpp
│   ├── model_client.cpp
│   ├── prompt_builder.cpp
│   ├── session_manager.cpp
│   ├── tool.cpp
│   ├── tool_policy.cpp
│   └── transcript_store.cpp
├── src/tools/
│   ├── echo_tool.cpp
│   ├── read_file_tool.cpp
│   ├── write_file_tool.cpp
│   ├── list_dir_tool.cpp
│   └── shell_tool.cpp
├── tests/
│   ├── transcript_store_tests.cpp
│   ├── session_manager_tests.cpp
│   ├── prompt_builder_tests.cpp
│   ├── tool_registry_tests.cpp
│   ├── tool_policy_tests.cpp
│   └── agent_loop_tests.cpp
└── examples/
    ├── config.example.json
    └── stdin_session.example.jsonl
```

---

## Runtime mode

The initial daemon communicates through newline-delimited JSON.

### Input envelope

```json
{"type":"user_message","session_id":"default","message":"List files in the workspace","workspace":"./workspace"}
```

### Output events

Emit one JSON object per line:

```json
{"type":"lifecycle","phase":"start","session_id":"default","turn_id":"..."}
{"type":"assistant_delta","text":"I will inspect the workspace."}
{"type":"tool_start","tool":"list_dir","call_id":"..."}
{"type":"tool_end","tool":"list_dir","call_id":"...","ok":true}
{"type":"assistant_final","text":"The workspace contains ..."}
{"type":"lifecycle","phase":"end","session_id":"default","turn_id":"..."}
```

Every event must be serializable, replayable, and safe to persist.

---

## Turn lifecycle

Implement the agent loop as an explicit state machine.

```text
ReceiveInput
  -> ValidateInput
  -> ResolveSession
  -> AcquireSessionLock
  -> AppendUserMessage
  -> BuildPrompt
  -> CallModel
  -> HandleAssistantText
  -> HandleToolCalls
  -> AppendToolResults
  -> ContinueOrFinish
  -> AppendAssistantFinal
  -> ReleaseSessionLock
```

Rules:

- Only one turn may run per session at a time.
- A global concurrency limit may cap simultaneous sessions.
- Every turn gets a unique `turn_id`.
- Every model tool call gets a unique `call_id`.
- Always persist user messages, tool calls, tool results, assistant final text, and errors.
- Partial assistant deltas may be emitted, but do not need to be persisted individually unless configured.
- The loop must support cancellation via `std::stop_token`.

---

## Transcript format

Use JSONL append-only logs.

Required event types:

```text
user_message
assistant_message
tool_call
tool_result
system_note
compaction_summary
error
```

Example:

```json
{"ts":"2026-01-01T00:00:00Z","type":"user_message","turn_id":"t1","text":"hello"}
{"ts":"2026-01-01T00:00:01Z","type":"assistant_message","turn_id":"t1","text":"Hi."}
```

Implementation requirements:

- Append must be atomic enough to avoid interleaved writes within a process.
- On startup, tolerate a truncated final line.
- Never load unbounded transcript history into memory.
- Provide `load_recent(session, budget)` using byte and approximate-token budgets.
- Provide `append(session, event)` with explicit error handling.
- Provide `compact(session)` that writes a summary and retains recent events.
- Use temp files and atomic rename for compaction.

---

## Memory management requirements

The project must be careful with allocations and object lifetimes.

### Ownership

- Use values and RAII wherever possible.
- Use `std::unique_ptr` for polymorphic ownership.
- Avoid shared ownership. `std::shared_ptr` requires a written justification in code review.
- No raw owning pointers.
- No global mutable state except immutable configuration after startup.

### Hot path allocation

For each turn:

- Create a turn-scoped `std::pmr::monotonic_buffer_resource`.
- Store temporary prompt fragments, parsed model response structures, and tool-call scratch buffers in that arena where practical.
- Do not return `string_view` or `span` into the arena beyond the turn.
- Do not store references to transcript buffers after compaction or reload.

### Bounds

Set default limits in config:

```json
{
  "limits": {
    "max_prompt_bytes": 262144,
    "max_tool_result_bytes": 65536,
    "max_shell_output_bytes": 65536,
    "max_turn_seconds": 300,
    "max_tool_seconds": 30,
    "max_concurrent_sessions": 4,
    "max_parallel_tools": 4
  }
}
```

All limits must be enforced.

---

## Tool calling design

### Tool interface

Implement tools behind a small interface:

```cpp
struct ToolParam {
    std::string name;
    std::string type;
    bool required;
    std::string description;
};

struct ToolDefinition {
    std::string name;
    std::string description;
    std::vector<ToolParam> params;
    bool dangerous = false;
};

struct ToolCall {
    std::string call_id;
    std::string name;
    JsonValue arguments;
};

struct ToolResult {
    std::string call_id;
    bool ok;
    JsonValue content;
    std::string error;
};

class ITool {
public:
    virtual ~ITool() = default;
    virtual ToolDefinition definition() const = 0;
    virtual ToolResult call(const ToolCall& call, const ToolContext& ctx) = 0;
};
```

The exact JSON abstraction may differ, but preserve the separation between:

- tool definition,
- model-requested tool call,
- validated execution context,
- result returned to the model.

### Built-in v0 tools

Implement only these tools initially:

1. `echo`: test utility.
2. `list_dir`: lists workspace-relative directory entries.
3. `read_file`: reads a workspace-relative UTF-8 text file.
4. `write_file`: writes a workspace-relative file, requiring explicit policy approval if overwriting.
5. `shell`: executes a command in the workspace with timeout and output limits. Disabled by default.

### Validation

Every tool call must pass:

1. Tool exists.
2. Arguments parse as JSON object.
3. Required parameters are present.
4. Parameter types match the definition.
5. Tool is allowed by policy.
6. Workspace paths are normalized and remain inside the workspace.
7. Tool-specific limits are enforced.
8. Dangerous tools are blocked unless explicitly enabled.

Do not execute unknown tools. Do not auto-correct dangerous tool names.

### Tool policy

Config example:

```json
{
  "tools": {
    "allow": ["echo", "list_dir", "read_file", "write_file"],
    "deny": ["shell"],
    "dangerous_requires_flag": true
  }
}
```

Policy order:

1. Explicit deny wins.
2. Unknown tool is denied.
3. Dangerous tools require both allowlist entry and explicit dangerous flag.
4. Workspace guard applies even if the tool is allowed.

### Shell tool

The shell tool is useful but risky.

Requirements:

- Disabled by default.
- No shell interpolation by default. Prefer exec-style argv arrays.
- Working directory must be the session workspace.
- Limit stdout and stderr bytes.
- Kill process group on timeout.
- Return exit code, truncated stdout, truncated stderr, and timeout flag.
- Never pass secrets into output logs.

---

## Workspace and path safety

All file tools accept workspace-relative paths.

Path rules:

- Reject absolute paths from model/tool arguments.
- Normalize `.` and `..`.
- Resolve symlinks where possible before final access.
- Reject paths that escape the workspace.
- Reject paths with embedded NUL.
- Treat the workspace as the only permitted file boundary for v0.

Create a `WorkspacePath` helper so this logic is centralized and tested.

---

## Model client

Do not hard-code a single provider throughout the runtime.

Use an interface like:

```cpp
class IModelClient {
public:
    virtual ~IModelClient() = default;
    virtual ModelResponse complete(const ModelRequest& request, std::stop_token stop) = 0;
};
```

`ModelRequest` should contain:

- system instructions,
- recent transcript messages,
- available tool definitions,
- model name,
- temperature or deterministic mode,
- max output limits.

`ModelResponse` should contain:

- assistant text deltas or final text,
- zero or more tool calls,
- finish reason,
- usage metadata if available,
- raw provider id/status for debugging.

The initial provider adapter may be simple, but the core loop must not know provider-specific JSON shapes beyond the adapter.

---

## Prompt policy

The agent should be direct and tool-oriented.

System prompt goals:

- Explain available tools.
- Tell the model to use tools only when needed.
- Require workspace-relative paths.
- Require brief final answers.
- Forbid revealing secrets from environment/config.
- Instruct the model to return structured tool calls when it needs action.

Prompt builder requirements:

- Keep a stable base prompt.
- Inject compact tool definitions.
- Inject memory summary if present.
- Inject recent transcript within a byte/token budget.
- Never inject entire large files automatically.
- Log prompt byte size and estimated token count.

---

## Compaction v0

Implement transcript compaction without vector memory.

Trigger compaction when:

- transcript exceeds configured byte size,
- prompt budget cannot include enough recent context,
- or user invokes a compact command.

Compaction output:

```json
{
  "type": "compaction_summary",
  "summary": "The user is building a C++ headless AI agent. Key decisions: ...",
  "covered_until_event": 1234
}
```

The first implementation may ask the model to summarize. If no model is configured, use a deterministic fallback summary that preserves session metadata and recent user goals.

---

## Configuration

Config file: `agent.json`.

Environment variables may override sensitive values.

Example:

```json
{
  "runtime": {
    "data_dir": ".agent",
    "default_workspace": "workspace",
    "jsonl_stdio": true
  },
  "model": {
    "provider": "generic_http",
    "model": "configured-model-name",
    "api_key_env": "AGENT_API_KEY",
    "endpoint_env": "AGENT_ENDPOINT"
  },
  "limits": {
    "max_prompt_bytes": 262144,
    "max_tool_result_bytes": 65536,
    "max_shell_output_bytes": 65536,
    "max_turn_seconds": 300,
    "max_tool_seconds": 30,
    "max_concurrent_sessions": 4,
    "max_parallel_tools": 4
  },
  "tools": {
    "allow": ["echo", "list_dir", "read_file", "write_file"],
    "deny": ["shell"],
    "enable_dangerous": false
  }
}
```

Never write API keys into transcripts, logs, crash reports, or test snapshots.

---

## Error handling

Use explicit error types.

Preferred pattern:

```cpp
Result<T, AgentError>
```

or `std::expected<T, AgentError>` if the chosen standard library supports it.

Every error should include:

- stable error code,
- human-readable message,
- optional underlying detail,
- whether it is retryable,
- safe-to-show flag.

Do not throw exceptions across module boundaries unless the whole project standardizes on exceptions. If exceptions are enabled internally, catch them at runtime boundaries and convert to `AgentError`.

---

## Logging and observability

Emit structured logs.

Required fields:

- timestamp,
- level,
- session_id if available,
- turn_id if available,
- component,
- message,
- safe metadata.

Do not log:

- API keys,
- authorization headers,
- full prompts by default,
- full tool outputs by default,
- environment variables.

Provide a debug mode that writes redacted prompt/tool traces to `.agent/debug/`.

---

## Testing requirements

Do not consider a feature complete without tests.

Minimum tests:

- Transcript append/load/recover truncated final line.
- Prompt builder respects byte budget.
- Tool registry rejects unknown tools.
- Tool policy deny wins over allow.
- Workspace path guard rejects `../`, absolute paths, symlink escapes, and NUL.
- `read_file` and `write_file` operate only inside workspace.
- Shell tool times out and truncates output.
- Session lock serializes simultaneous turns for same session.
- Compaction writes summary using temp file and atomic rename.
- Agent loop can perform a mock model response with one tool call and final answer.

Use mock `IModelClient` for agent loop tests.

---

## Performance targets for v0

Measure these with a simple benchmark or integration test:

- Cold start under 100 ms where practical, excluding dynamic library and network overhead.
- Append transcript event under 1 ms for normal local disk.
- Load recent context without scanning more than configured max bytes when possible.
- Execute no-tool mock turn under 10 ms excluding model call.
- Keep resident memory stable across 1,000 mock turns in one session.
- No unbounded growth in tool registry, prompt builder, or event buffers.

These are targets, not hard blockers, but regressions should be visible.

---

## Security baseline

This is a local tool-using agent. Treat model output as untrusted input.

Required safeguards:

- Validate every model-generated tool call.
- Default-deny dangerous tools.
- Enforce workspace path boundaries.
- Enforce command timeouts and output limits.
- Redact secrets from logs and tool results.
- Avoid network-capable tools in v0 except the model provider adapter.
- Make destructive actions explicit and configurable.
- Do not persist hidden/system secrets in transcripts.

---

## CLI behavior

`agent_cli` should support:

```bash
agent_cli --config agent.json --session default --workspace ./workspace "summarize README.md"
```

It should print final assistant output to stdout and diagnostic logs to stderr.

`agentd` should support JSONL stdin/stdout mode:

```bash
printf '%s\n' '{"type":"user_message","session_id":"default","message":"hello"}' | agentd --config agent.json
```

---

## Implementation milestones

### Milestone 1: skeleton ✓

- CMake project.
- Config loader.
- JSON wrapper (nlohmann/json behind `JsonValue`/`parse_json` abstraction).
- Event model (runtime events + transcript records).
- CLI argument parsing.
- Basic structured JSONL logging via `emit_event()`.

### Milestone 2: sessions and transcripts ✓

- `SessionManager` — maps session IDs to workspace paths and `TranscriptStore`.
- Workspace creation on first access.
- `TranscriptStore` — append-only JSONL log with `append()`, `load_recent()`, `compact()`.
- `SessionState::turn_mutex` serializes concurrent turns per session.
- Recovery from truncated final JSONL line (parse errors are silently skipped).

### Milestone 3: tools ✓

- `ITool` / `ToolRegistry` / `ToolPolicy` interfaces.
- `resolve_workspace_path()` — centralized path guard; tested for `../`, absolute, symlink, NUL.
- `echo`, `list_dir`, `read_file`, `write_file` — all workspace-confined.
- `shell` — exec-style, timeout, output truncation; dangerous flag; disabled by default.

### Milestone 4: model loop ✓

- `IModelClient` interface with `DeltaCallback` streaming.
- `AnthropicClient` — Anthropic Messages API adapter (libcurl).
- `OpenAIClient` — OpenAI-compatible adapter (libcurl).
- `PromptBuilder` — byte-budgeted prompt from transcript history.
- Tool-call continuation loop in `AgentRuntime::model_iteration()` (max 16 iterations).
- Full JSONL event streaming: lifecycle, assistant_delta, tool_start/end, assistant_final, error.
- Mock `IModelClient` used in all agent loop tests.

### Milestone 5: compaction and limits ✓

- Prompt byte budget enforced in `PromptBuilder::build()`.
- `TranscriptStore::compact()` — writes `compaction_summary`, retains recent tail, atomic rename.
- Tool result size bounded (`max_tool_result_bytes`).
- Shell output bounded (`max_shell_output_bytes`), per-tool timeout (`max_tool_seconds`).
- API keys resolved from env at call time; never stored in transcripts or logs.

---

## Definition of done for v0

The initial release is done when:

- A user can run `agent_cli` against a local workspace.
- The runtime can process JSONL user messages through `agentd`.
- The agent can call safe file tools.
- Tool calls are validated and policy-gated.
- Sessions are persisted as JSONL transcripts.
- Recent context and compaction keep prompts bounded.
- Memory usage remains stable in long mock-turn tests.
- Dangerous shell execution is disabled by default and guarded when enabled.
- The codebase has tests for core safety and lifecycle behavior.
- The project builds from a clean checkout with documented commands.

---

## Non-goals for v0

Do not implement these unless explicitly requested after v0:

- Messaging channels.
- Plugin SDK.
- Browser automation.
- Vector long-term memory.
- Multi-agent routing.
- Web dashboard.
- Remote administration.
- Distributed workers.
- Full sandboxing with containers.
- Voice, image, or document ingestion.
- Tool marketplace or dynamic untrusted plugins.

---

## Coding style

- Keep headers small and explicit.
- Prefer composition over inheritance except for narrow interfaces.
- Keep functions short and testable.
- Use clear names over abbreviations.
- Treat warnings as errors in CI.
- Format consistently with `clang-format`.
- Run static analysis where available.
- Keep public interfaces documented with short comments.
- Do not introduce hidden global state.
- Do not add dependencies without updating this file and explaining why.

---

## Review checklist for coding agents

Before submitting code, verify:

- Does it preserve bounded memory behavior?
- Does it avoid loading unbounded transcripts?
- Are all model-generated inputs validated?
- Are workspace path rules centralized and tested?
- Are tool outputs truncated/redacted?
- Are sensitive values excluded from logs/transcripts?
- Are errors explicit and safe to show when user-facing?
- Are tests included or updated?
- Does the implementation avoid frameworks and unnecessary dependencies?
- Can the feature be exercised through CLI or JSONL mode?


## Repository review workflows

For complete queues or repository-wide audits, use the canonical local skills:

- [review-bugs](.agents/skills/review-bugs/SKILL.md): reproduce and resolve the open bug queue within existing authority.
- [review-documentations](.agents/skills/review-documentations/SKILL.md): reconcile all documentation against source and tests.
- [review-enhancements](.agents/skills/review-enhancements/SKILL.md): classify enhancement requests without implementing them.
- [review-prs](.agents/skills/review-prs/SKILL.md): review and test the open PR queue; merge and clean up when authorized.

Claude discovery uses relative symlinks under `.claude/skills/` to the same
canonical folders. Keep skill metadata and links synchronized. Ordinary focused
changes follow this file without requiring a whole-queue workflow.

Use [CODE_REVIEW.md](CODE_REVIEW.md) for review findings and severity. Keep
[CLI/config/JSONL contracts](docs/cli.md), [local setup](docs/host-setup.md),
help output, examples, and tests synchronized with public behavior. Record
notable changes in `CHANGELOG.md`, architectural decisions in `docs/decisions/`,
and outstanding acceptance gaps in `docs/status.md`.
