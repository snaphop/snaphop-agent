# Maintenance status — 2026-10-10

The October maintenance pass refreshes dependencies and repairs the existing
v0 runtime. It preserves the original local, headless scope.

Completed work:

- Pinned nlohmann/json 3.12.0 and Catch2 3.16.1 with verified archive hashes.
  Sources: [JSON releases](https://github.com/nlohmann/json/releases/tag/v3.12.0)
  and [Catch2 release](https://github.com/catchorg/Catch2/releases/tag/v3.16.1).
- Native provider tool continuations, nullable OpenAI content handling, and
  consistent HTTP/cancellation errors. Protocol references:
  [OpenAI Chat Completions](https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create)
  and [Claude tool calls](https://platform.claude.com/docs/en/agents-and-tools/tool-use/handle-tool-calls).
- Serialized prompt budgets, bounded HTTP responses and directory results,
  turn-scoped event storage, and a 1,000-turn performance check.
- Optional argument validation, new-file symlink guards, session ID validation,
  explicit overwrite policy, and configured API credential redaction.
- Turn deadlines, a runtime-wide concurrency cap, bounded session caching,
  and shell process-group handling after output pipes close.
- Transcript tail recovery before append, bounded compaction, automatic
  transcript-size compaction, and cumulative summary coverage counters.
- Plain CLI answers, malformed JSONL envelope handling, local provider smoke
  tests.

Validation on Linux with GCC 15.2:

- Release: 61 tests passed, plus local HTTP protocol checks for both providers.
- Debug with AddressSanitizer/UndefinedBehaviorSanitizer and leak detection:
  61 tests passed, plus the same protocol checks, without sanitizer findings.
- A fresh `BUILD_TESTING=OFF` build produces both runtime executables and passes
  the local protocol check without downloading Catch2.
- Release observation: 1,000 mock turns averaged 0.54 ms per turn with a
  4 KiB RSS increase between turn 100 and turn 1,000. The prompt budget for this
  check is 4 KiB; this is not a benchmark of the default 256 KiB context or a
  live provider. Sanitizer allocator quarantine changes RSS measurements.
- CLI `--help` startup: median 6.21 ms over ten runs. This measures process
  startup/help, not a model turn, and does not establish first-load disk latency.

Tool-calling follow-up (OpenClaw checkout `654c8599da7`, 2026-10-10):

- Paginated UTF-8 file reads reserve serialized JSON costs and expose source-byte
  continuation offsets. Redaction cannot silently invalidate a cursor.
- Tool result budgeting retains useful text, metadata, and command diagnostics;
  shell capture keeps a bounded head and tail. Prompt-pressure trimming preserves
  native call/result pairing and persisted evidence.
- A 32-record turn-local progress guard warns on stalled repetition and stops
  continued repeats. Independent limits cap total calls at 128, response batches
  at 32, and model iterations at 16.
- Cached definitions and canonical schemas validate unknown arguments, numeric
  bounds, and array elements. Tool registration freezes before preparation.
  Stable context loads once per turn and messages extend incrementally.
- Read-only batches share lazy, bounded workers and a 32-job queue. Mixed or
  rejected batches stay sequential; results persist in model call order and
  active work settles before turn state is released.
- Interrupted replay preserves completed effects, marks missing outcomes as
  unknown, and never reruns tools. Local execution IDs are unique while separate
  provider IDs retain native wire correlation, including repeated provider IDs.

Follow-up validation:

- Release and AddressSanitizer/UndefinedBehaviorSanitizer with leak detection:
  85 tests passed in each build (6,658 assertions).
- Local HTTP checks for both providers passed in Release, sanitizer, and
  `BUILD_TESTING=OFF` runtime builds. Checks include parallel native tool batches,
  persisted replay in a fresh CLI process, schema stability, and malformed JSONL.
- Release observations with 4 KiB prompts: 1,000 no-tool turns averaged 0.45 ms
  with no observed RSS growth after warmup. Two-tool echo turns averaged 0.89 ms
  sequentially and 0.94 ms with four workers; warmup-to-final RSS growth was zero
  and 8 KiB, respectively. Echo measures scheduling overhead for cheap tools;
  independent work with appreciable latency is the target for parallelism.
  These observations exclude live providers and are not hard performance gates.

Remaining v0 release work:

1. Add true SSE streaming and wire daemon signal handling to active-turn
   cancellation. Today a daemon signal sets its shutdown flag; a running turn
   still relies on its deadline or provider completion.
2. Finish stable error codes and structured diagnostics, including prompt
   byte/token metrics and the redacted debug trace mode.
3. Harden filesystem operations against concurrent symlink substitution using
   descriptor-based access. Static symlink escapes are tested; the current
   resolve-then-open sequence is not race resistant.
4. Separate shell stdout and stderr, constrain inherited secrets, and add
   configurable secret redaction beyond the configured provider API key.
5. Add explicit compact commands and compaction on context pressure; summaries
   currently preserve metadata and recent goals, not all historical decisions.
6. Add safe session-cache eviction/reopening semantics and bounded key-value
   note reads. Cached sessions are capped rather than automatically evicted.
7. Validate provider-specific model settings against actual account access.
   All automated provider tests use local fakes; no paid provider request is
   part of this maintenance pass.

The original milestone checkmarks describe modules being present. They should
not be read as proof that every v0 acceptance criterion is complete. Continue
with these remaining items before calling the release finished.
