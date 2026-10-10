# SnapHop Agent

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

The complete local verification command is `scripts/check.sh` (requires Python 3
and Cppcheck 2.13+, plus Trivy 0.75.0, in addition to the build prerequisites).
It builds with warnings as errors, tests build-script orchestration, runs the C++ regression
suite, checks both provider adapters against a loopback HTTP server, and runs
static analysis and security scans. No provider account or API key is needed.
The initial CMake configure downloads the pinned build dependencies; subsequent runs can use the cached sources.

Build scripts run concurrent compiler jobs using `BUILD_JOBS`, then
`CMAKE_BUILD_PARALLEL_LEVEL`, or the local CPU count by default. For example,
`BUILD_JOBS=2 scripts/check.sh` caps compilation on a machine with limited RAM.
Tests run sequentially because some existing fixtures share process resources.

Install the static analyzer on Ubuntu with `sudo apt-get install cppcheck`.
Run it independently with `scripts/static-analysis.sh`, or pass a build
directory as the first argument. It configures CMake without requiring a build,
then analyzes all configured runtime and test sources (including project
headers) using the actual include paths and defines in `compile_commands.json`.
Use `CPPCHECK=/path/to/cppcheck` to select a different executable. Analysis uses
the same parallel-job settings as compilation.

Errors, warnings, performance and portability diagnostics fail the local and
CI checks. Diagnostics from downloaded dependencies are excluded; Catch2 macro
compatibility definitions apply only to analysis and preserve test expressions.
The log is saved to `build/static-analysis/cppcheck.log`, including on failure,
and uploaded by CI. Analysis caches also stay inside the build directory.

Install and run the required security scanner:

```bash
scripts/install-trivy.sh
scripts/security-check.sh build
# scripts/check.sh also runs this gate after building and testing.
```

The installer verifies a pinned Trivy archive SHA256. Set `TRIVY=/path/to/trivy`
to use another installation. Security scans cover authored repository files,
optional Python requirements, pinned C++ dependencies through a generated
CycloneDX SBOM, and distro packages owning the built executables' linked
libraries. Vulnerability, misconfiguration and secret findings at every severity
fail verification, including unfixed vulnerabilities. Full license scanning
produces an informational inventory. Scan errors and missing package coverage
also fail; zero detected packages cannot silently pass the dependency checks.

Private reports live in `build/security/` with restricted permissions. They are
not printed or uploaded publicly. See [security-check scope and handling](docs/security-checks.md)
for exclusions, Debian/Ubuntu prerequisites, database downloads and limitations.

Measure coverage locally using GCC and its matching gcov:

```bash
python3 -m venv .venv-coverage
.venv-coverage/bin/python -m pip install -r scripts/requirements-coverage.txt
PYTHON=.venv-coverage/bin/python scripts/coverage.sh
# For a versioned toolchain, also set CC=gcc-14 CXX=g++-14 GCOV=gcov-14.
```

Open `build-coverage/coverage/index.html` for per-line details. Cobertura XML and
JSON summaries are in the same directory. Instrumentation includes project
headers, both executables, and every library source; dependencies and test code
are outside the report. Only compiler-generated exception/unreachable branches
are omitted from branch totals. Each run clears old execution counters.
Coverage builds use a separate Debug directory and do not affect release flags.

CI runs the same local suite and publishes coverage artifacts, enforcing current
regression floors of 92% lines and 80% branches. These floors are below the
requested 100% target: full coverage is still outstanding. To enforce 100% and
get a failing exit status until the remaining paths are tested, run:

```bash
COVERAGE_MIN_LINES=100 COVERAGE_MIN_BRANCHES=100 \
  PYTHON=.venv-coverage/bin/python scripts/coverage.sh
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
- [`docs/README.md`](./docs/README.md) — documentation index and maintenance status
- [`docs/cli.md`](./docs/cli.md) — CLI, JSONL and configuration contract
- [`docs/host-setup.md`](./docs/host-setup.md) — local development setup and verification
- [`docs/security-checks.md`](./docs/security-checks.md) — Trivy scope, reports and finding handling
- [`CODE_REVIEW.md`](./CODE_REVIEW.md) — review process and runtime safety priorities
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

`AGENTS.md` is the source of truth. Every other agent instruction file defers to it.

Repository review skills live in [`.agents/skills/`](./.agents/skills/), with
Claude symlinks in [`.claude/skills/`](./.claude/skills/): `review-bugs`,
`review-documentations`, `review-enhancements`, and `review-prs`. See the
[skill catalog](./CONTRIBUTING.md#repository-review-skills) for their scopes and
[comparison record](./docs/repository-parity.md) for parity with snaphop-agent-vm.

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
