# Architecture

snaphop-agent is a local, headless C++23 runtime. `agent_cli` runs one turn and
prints the final answer; `agentd` processes JSONL input sequentially. Both use
the same runtime. Only provider adapters perform implicit network requests.

```text
CLI / JSONL stdin -> AgentRuntime -> JSONL events / final CLI output
                       |
        +--------------+---------------+
        |              |               |
  SessionManager   IModelClient    ToolRegistry + ToolPolicy
        |              |               |
 TranscriptStore   libcurl HTTP    echo / file tools / optional shell
 MemoryManager     provider API   workspace path guard
```

`AgentRuntime` validates input, resolves a session, waits for its turn lock and
an execution slot, persists the user message, builds context, calls the model,
validates and dispatches tools, and persists the final answer. A maximum of 16
model iterations, 32 calls per response, and 128 calls per turn bounds the
continuation loop. A bounded progress guard warns and stops stalled repetition.
Cancellation and the turn deadline propagate through `std::stop_token` to the
provider transport, tool workers, and shell runner. Individual file operations
are synchronous; independent read-only calls can share a bounded worker pool.
Mixed batches with writes or shell commands execute sequentially. Only the
turn thread emits events and persists results, in model call order. Worker
threads start lazily and are joined by runtime ownership; all submitted work
settles before turn-local state is destroyed.

Sessions own their workspace, transcript store, and turn mutex. Runtime-wide
execution slots enforce `max_concurrent_sessions`; a separate
`max_cached_sessions` bounds retained session objects. Cached sessions are not
automatically evicted. Runtime event writes are serialized; the CLI selects
stderr as its event destination at startup.

Transcripts are append-only JSONL. Replay seeks into a byte-bounded tail;
append removes an unterminated final record before adding new events. Transcript
operations on a store are protected by its mutex. Compaction counts records
with fixed-size reads, saves a deterministic summary of recent user goals,
retains a bounded tail, and replaces the log by rename. The runtime triggers
compaction at `max_transcript_bytes`. Full diagnostic replay has an explicit
byte bound. Rename provides atomic visibility, without an fsync durability
promise.

Prompt replay retains native assistant calls and their associated tool results.
Completed effects survive interrupted exchanges; missing results become explicit
unknown-outcome errors and are never executed during replay. Displaced results
are paired and duplicates/orphans are removed only from the model view. Older
turns are removed as whole groups. The budget includes the serialized provider
request, including schemas and escaping. Under pressure, older tool payloads can shrink while
keeping pairing and persisted evidence. A current turn that still cannot fit
returns an error. Stable context and cached canonical schemas are prepared once
per turn, and messages extend incrementally after tool results. A
turn-scoped PMR arena owns the temporary event vector; JSON values and model
messages still use ordinary bounded allocations.

`IModelClient` keeps provider details out of the loop. Anthropic Messages and
OpenAI-compatible Chat Completions serialize calls and results differently.
Pure request serializers are covered by tests, and a local fake HTTP provider
exercises both adapters end to end. HTTP responses are limited to 1 MiB by
default and use cancellable libcurl transfers. These adapters buffer responses
and emit text chunks after receipt; they do not implement SSE streaming yet.

Tools cache definitions at registration. Frozen registries prepare immutable
validated calls; canonical schemas include parameter types, array element types,
numeric bounds, and rejection of unknown arguments. Providers project the same
schemas into their native request shape. Paginated file reads reserve serialized
metadata/escaping costs and return UTF-8-safe continuation offsets. Tool outputs
retain useful text and metadata within their content budget. File paths are
resolved centrally, including existing symlink parents of new files. Overwrites require
both the model's `overwrite` argument and operator policy. Shell is disabled by
default and requires the dangerous-tool flag plus policy approval. Enabled
commands receive explicit argv, a workspace working directory, bounded output,
and process-group cancellation. Shell capture retains a bounded head and tail
for late diagnostics. Shell is not a filesystem sandbox.

Configured provider credentials are redacted from runtime text and persisted
tool values. This is exact-value redaction, not detection of arbitrary secrets
inside files or command output. Provider response/error bodies are not logged.

Build dependencies are libcurl, nlohmann/json, and (when `BUILD_TESTING=ON`)
Catch2. No framework, scripting runtime, messaging gateway, or plugin system is
part of the runtime. See [status.md](status.md) for remaining release work.
