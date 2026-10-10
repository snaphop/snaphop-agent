# snaphop-agent

[![Verify](https://github.com/snaphop/snaphop-agent/actions/workflows/verify.yml/badge.svg)](https://github.com/snaphop/snaphop-agent/actions/workflows/verify.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](./LICENSE)

> Fast, headless, local-first AI agent runtime in C++23.

A compact, framework-free agent runtime designed for low-latency tool-calling workloads. Driven from a terminal or JSONL stdin/stdout.

## Quick start

```bash
git clone https://github.com/snaphop/snaphop-agent.git
cd snaphop-agent

# Build (Linux; CMake 3.20+, GCC 13+ or Clang 16+ with a C++23 library, libcurl headers)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run tests
build/agent_tests

# Interactive turn (requires AGENT_API_KEY env var)
export AGENT_API_KEY=<your-api-key>
cp examples/config.example.json agent.json
# Set model.model in agent.json to a model ID available to your provider account.
build/agent_cli --workspace ./workspace "List the files in the workspace"

# JSONL daemon mode
printf '%s\n' '{"type":"user_message","session_id":"default","message":"hello"}' \
  | build/agentd --config agent.json
```

## Architecture

```
stdin / IPC ---> AgentRuntime / Hub ---> stdout events
                      |
       +--------------+--------------+
       |              |              |
  SessionStore   ModelClient    ToolRegistry
       |              |              |
  Transcript     Provider API   Built-in tools
  Compaction     HTTP adapters  Shell / FS
```

See [`docs/architecture.md`](./docs/architecture.md) for detail.

## Configuration

Copy `examples/config.example.json` to `agent.json` and set:

| Env var           | Purpose                              |
|-------------------|--------------------------------------|
| `AGENT_API_KEY`   | API key for the model provider       |
| `AGENT_ENDPOINT`  | Override default endpoint URL        |

The default provider is Anthropic (Claude). Set `"provider": "openai"` in
`agent.json` for an OpenAI-compatible endpoint.

## Built-in tools (v0)

| Tool         | Safe | Description                                   |
|--------------|------|-----------------------------------------------|
| `echo`       | ✓    | Returns its input — useful for testing        |
| `list_dir`   | ✓    | Lists a workspace-relative directory          |
| `read_file`  | ✓    | Reads a workspace-relative UTF-8 file         |
| `write_file` | ✓    | Writes a workspace-relative file              |
| `shell`      | ✗    | Exec-style shell (disabled by default)        |

All file paths are validated and confined to the session workspace. Absolute
paths, `..` traversal, symlink escapes, and NUL bytes are rejected.

## Build targets

| Target        | Description                              |
|---------------|------------------------------------------|
| `agent_lib`   | Core library (linked by all targets)     |
| `agentd`      | Headless JSONL daemon                    |
| `agent_cli`   | Interactive CLI for manual testing       |
| `agent_tests` | Unit and integration tests (Catch2)      |

## JSONL protocol

**Input envelope** (one per line on stdin):
```json
{"type":"user_message","session_id":"default","message":"List files","workspace":"./workspace"}
```

**Output events** (emitted to stdout):
```json
{"type":"lifecycle","phase":"start","session_id":"...","turn_id":"..."}
{"type":"assistant_delta","text":"I will inspect..."}
{"type":"tool_start","tool":"list_dir","call_id":"..."}
{"type":"tool_end","tool":"list_dir","call_id":"...","ok":true}
{"type":"assistant_final","text":"The workspace contains..."}
{"type":"lifecycle","phase":"end","session_id":"...","turn_id":"..."}
```

## Resource limits

Defaults are in `examples/config.example.json`. Turn deadlines and cancellation
are cooperative; synchronous file operations cannot be forcibly interrupted.

- `max_prompt_bytes`: 256 KB
- `max_tool_result_bytes`: 64 KB
- `max_shell_output_bytes`: 64 KB
- `max_turn_seconds`: 300
- `max_tool_seconds`: 30
- `max_concurrent_sessions`: 4 running turns
- `max_cached_sessions`: 64 retained session objects (no automatic eviction)
- `max_transcript_bytes`: 4 MiB, triggering deterministic compaction

Overwriting an existing file requires both `overwrite: true` in the tool call
and `tools.allow_overwrite: true` in operator configuration. Shell remains
blocked by default.

Provider adapters currently buffer responses before emitting text events.
See [maintenance status](docs/status.md) for remaining v0 work.

Run the local HTTP protocol smoke test without provider credentials:

```bash
python3 scripts/check_protocol.py build
```

Run the performance observation separately:

```bash
build/agent_tests '[performance]'
```

Build runtime targets without downloading the test framework:

```bash
cmake -S . -B build-runtime -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build-runtime --parallel
```

## Documentation

- [`AGENTS.md`](./AGENTS.md) — canonical instructions for AI coding agents
- [`docs/architecture.md`](./docs/architecture.md) — system design overview
- [`docs/decisions/`](./docs/decisions/) — Architecture Decision Records
- [`CONTRIBUTING.md`](./CONTRIBUTING.md) — how to propose changes
- [`SECURITY.md`](./SECURITY.md) — how to report vulnerabilities
- [`CHANGELOG.md`](./CHANGELOG.md) — release history

## Agent files

| File                                | Tool                              | Status       |
|-------------------------------------|-----------------------------------|--------------|
| `AGENTS.md`                         | Codex, OpenCode, Aider, Amp, Roo, Continue, Copilot, Windsurf | **canonical** |
| `CLAUDE.md`                         | Claude Code (imports `@AGENTS.md`)| active       |
| `GEMINI.md`                         | Gemini CLI                        | active       |

`AGENTS.md` is the source of truth. Every other file defers to it.

## License

[MIT](./LICENSE), copyright 2026 SnapHop. Third-party notices are in
[NOTICE](./NOTICE).

## Contributing and releases

Use [GitHub issues](https://github.com/snaphop/snaphop-agent/issues) for bugs and
feature requests, and [CONTRIBUTING.md](./CONTRIBUTING.md) for development.
Report suspected vulnerabilities privately to <security@snaphop.com> as described
in [SECURITY.md](./SECURITY.md).

`scripts/check.sh` builds and runs the regression and local protocol checks.
GitHub Actions runs it on pull requests and pushes to `master`. Pushing a
`vX.Y.Z` tag runs the same checks and publishes a GitHub Release containing
the Linux x86_64 runtime archive, `LICENSE`, `NOTICE`, and `SHA256SUMS`.
Prerelease tags such as `v0.1.0-rc.1` produce prereleases. Merges do not publish.

Build an archive locally with `VERSION=v0.1.0 scripts/build-release.sh`.
The archive contains `bin/agent_cli`, `bin/agentd`, example configuration, and
documentation. These are dynamically linked Linux binaries: GitHub releases
are built on Ubuntu 24.04 with GCC 14 and require compatible libcurl, libstdc++,
and glibc runtime libraries. Build from source on other systems. Local archives
inherit the local system's library requirements.

The [migration record](./docs/github-migration.md) links the earlier Gitea PRs.

## Bounded tool output

`read_file` accepts optional `offset` (zero-based byte offset, default `0`) and
`limit` (positive source-byte count, capped by `max_tool_result_bytes`). Offsets
must fall on UTF-8 code point boundaries. Results include `content`, `path`,
`offset`, `next_offset`, and `truncated`. When `truncated` is true, pass
`next_offset` as the next call's `offset`; continue until `truncated` is false.
For example: `{"path":"README.md","offset":0,"limit":4096}`.

The serialized result content, including JSON escaping and pagination metadata,
fits `max_tool_result_bytes`. A budget too small for metadata or one code point
returns an explicit error. Files must remain unchanged while paging; byte offsets
are not snapshots. Invalid UTF-8 pages are rejected.

Oversized tool output retains useful text and structured metadata where possible;
command output retains a diagnostic tail. During prompt pressure, older tool
payloads can be shortened in the model view while preserving call/result pairing
and persisted evidence. The latest result is retained whole when it fits. Calls,
system instructions, or user input that still cannot fit cause a budget error.

## Tool execution and recovery

The tool registry caches definitions at registration and is frozen when the
runtime takes ownership. One canonical schema drives input validation and both
provider adapters, including array element types, numeric bounds, and rejection
of unknown arguments. Tool descriptions live in native schemas; the system
prompt includes only their names. Schemas, tool ordering, and memory context stay
stable during a turn, and model messages are extended after each tool batch.

A batch containing only validated, policy-approved read-only calls can execute
concurrently. The runtime shares a lazy worker pool capped by
`limits.max_parallel_tools` (default `4`, range `1`–`32`) and a 32-job queue.
Set the limit to `1` to execute all calls sequentially. Batches containing writes,
shell calls, or rejected calls run sequentially. Events and persisted results
remain in model call order, and active work settles before turn state is freed.
Read-only status is an explicit thread-safety contract; arbitrary tools default
to sequential execution. Parallel reads do not provide a filesystem snapshot.

A turn-local 32-record progress guard warns after three unchanged observations
of the same action and stops continued repetition at five. Changed observations,
new actions, and successful mutations count as possible progress. Repeated
errors and alternating stalled actions are covered. The runtime also enforces
128 total calls per turn, 32 calls per response, and 16 model iterations.
Duplicate call IDs within one response reject the batch before execution;
provider IDs may repeat in later assistant batches. Every execution receives a
unique local `call_id`; transcripts retain the separate `provider_call_id` for
wire replay. Older logs without that field continue to replay.

Interrupted transcript replay retains completed tool results and supplies an
explicit unknown-outcome error for missing results. Displaced results are paired
with their assistant calls and duplicates/orphans are removed from the model
view. Recovery never reruns tools; inspect state before retrying a mutation with
an unknown outcome. Replay repairs and prompt trimming leave the log intact.
