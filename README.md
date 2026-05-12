# snaphop-agent

> Fast, headless, local-first AI agent runtime in C++23.

A compact, framework-free agent runtime designed for low-latency tool-calling workloads. Driven from a terminal, JSONL stdin/stdout, or a local IPC boundary.

## Quick start

```bash
# Build (requires cmake 3.20+, a C++23-capable compiler, libcurl dev headers)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run tests
build/agent_tests

# Interactive turn (requires AGENT_API_KEY env var)
export AGENT_API_KEY=<your-api-key>
cp examples/config.example.json agent.json
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
  Compaction     Streaming      Shell / FS / HTTP
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

All limits are enforced; defaults are in `examples/config.example.json`:

- `max_prompt_bytes`: 256 KB
- `max_tool_result_bytes`: 64 KB
- `max_shell_output_bytes`: 64 KB
- `max_turn_seconds`: 300
- `max_tool_seconds`: 30
- `max_concurrent_sessions`: 4

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

[MIT](./LICENSE)
