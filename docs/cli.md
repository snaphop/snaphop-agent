# CLI and JSONL contract

This describes the current entry points, not planned v0 behavior. Source:
[agent_cli](../src/main_agent_cli.cpp), [agentd](../src/main_agentd.cpp),
[configuration](../include/agent/config.hpp), and
[config parsing](../src/config.cpp). See [setup](host-setup.md) for local checks.

## Terminal turn

```bash
build/agent_cli --config agent.json --session default --workspace ./workspace "Summarize README.md"
```

| Option | Default / behavior |
|---|---|
| `--config <path>` | `agent.json`; a missing file uses built-in defaults. |
| `--session <id>` | `default`. |
| `--workspace <path>` | `runtime.default_workspace` when omitted. |
| `--help`, `-h` | Print usage to stderr and exit successfully. |

Pass one nonempty message argument, quoting spaces. Success writes the final
assistant text plus a newline to stdout. Runtime events use JSONL on stderr;
argument/configuration failures can use plain stderr text. Exit status is `0`
on success/help and `1` on invalid arguments, configuration, or failed turn.
This is a single-turn CLI, not an interactive REPL.

## JSONL daemon

```bash
printf '%s\n' '{"type":"user_message","session_id":"default","message":"hello","workspace":"./workspace"}' \
  | build/agentd --config agent.json
```

`agentd` accepts `--config <path>`, `--help`, and `-h`. It reads one JSON object
per line and processes envelopes sequentially. Blank lines are ignored.
`type` must be `user_message`; `message` must be a nonempty string.
`session_id` defaults to `default`, and an omitted/empty `workspace` uses the
configured default. These fields must be strings when supplied. Unknown extra
fields are currently ignored. Session IDs are 1–128 characters from ASCII
letters, digits, `_`, `-`, and `.`, excluding `.` and `..`.

stdout contains JSONL events: `lifecycle` (`start`/`end`), `assistant_delta`,
`tool_start`, `tool_end`, `assistant_final`, and `error`. Fields depend on the
event type; identifiers and tool names accompany their relevant events. See
[event constructors](../src/event.cpp) and the [README examples](../README.md#jsonl-protocol).
Errors can occur before lifecycle start. Invalid envelopes emit errors and
processing continues; EOF normally exits `0`, even if individual turns failed.
Argument/configuration failures exit `1`. Consumers must inspect events rather
than treating the daemon process exit code as each turn's result.

The runtime supports concurrent callers, but stdin processing itself is serial.
Provider text events currently follow buffered HTTP responses. Signal handling
sets a daemon shutdown flag; it does not yet cancel the active turn. There are
no compact, cancel, or IPC commands in this entry point today.

## Configuration and credentials

Start from [config.example.json](../examples/config.example.json). `runtime`
contains `data_dir` (default `.agent`), `default_workspace` (`workspace`), and
`jsonl_stdio` (`true`). The current daemon always uses JSONL; `jsonl_stdio` is
parsed but does not select another transport.

`model` contains `provider`, `model`, `api_key_env`, and `endpoint_env`.
`openai` selects the OpenAI-compatible adapter. `anthropic` and the built-in
`generic_http` default select the Anthropic-compatible adapter; other provider
names currently fall through to that adapter. The example sets `anthropic` and
a model placeholder; built-in defaults have no model ID. Set a model available
to your account. Credentials and endpoint overrides are read from the named
environment variables, defaulting to `AGENT_API_KEY` and `AGENT_ENDPOINT`.
Endpoint overrides are complete request URLs. Defaults are the Anthropic
Messages or OpenAI Chat Completions endpoints. Do not put credentials in config.

| Limit | Default |
|---|---:|
| `max_prompt_bytes` | 262144 |
| `max_transcript_bytes` | 4194304 |
| `max_tool_result_bytes` | 65536 |
| `max_shell_output_bytes` | 65536 |
| `max_turn_seconds` | 300 |
| `max_tool_seconds` | 30 |
| `max_concurrent_sessions` | 4 |
| `max_cached_sessions` | 64 |
| `max_parallel_tools` | 4 |

Limits must be positive integers no greater than `UINT32_MAX`;
`max_parallel_tools` is additionally capped at 32. Turn/file cancellation is
cooperative. Cached sessions have a cap without automatic eviction.

`tools.allow` defaults to `echo`, `list_dir`, `read_file`, and `write_file`;
`tools.deny` defaults to `shell`. Explicit deny wins. `enable_dangerous` and
`allow_overwrite` default to false. Shell needs an allow entry, removal from deny,
and the dangerous flag. Overwriting needs both operator `allow_overwrite` and
tool argument `overwrite: true`. File tool arguments are workspace-relative;
operator workspace selection may use an absolute directory.

Transcripts live under `<data_dir>/sessions/<session_id>/transcript.jsonl`.
Within a running process, a cached session retains the workspace selected when
it was first created. Later overrides for that session do not replace it.
Transcripts are append-only event records, distinct from stdout lifecycle events.
Compaction preserves a summary and recent tail. Session locks are process-local;
use one runtime process per shared data directory to avoid cross-process races.
See [architecture](architecture.md) and [status](status.md) for detailed behavior
and known safety/acceptance gaps.
