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
8. Reach the requested 100% code coverage. The GCC 15.2 Debug coverage run
   currently measures 94.1% lines (1,956/2,079) and 84.2% branches
   (2,470/2,932), including project headers and both entry points. The CLI and
   configuration loader have 100% line coverage. Runtime recovery and low-level
   I/O/process failure paths remain uncovered; no project source lines are
   excluded. Compiler-generated exception/unreachable branches are excluded
   from branch totals. `scripts/coverage.sh` reports every gap and supports
   explicit 100% gates; CI currently enforces 92% lines and 80% branches.

Local development follow-up validation:

- `scripts/check.sh`: 103 C++ tests, four build-script tests, and expanded
  loopback provider/CLI/JSONL checks passed in Release with warnings as errors.
- `scripts/coverage.sh`: the same 103 C++ tests and loopback checks passed in
  Debug with GCC coverage instrumentation and atomic profile counter updates.
- Reports are emitted as detailed HTML, Cobertura XML, and a JSON summary.
  Counters are cleared before each run; coverage dependencies are optional
  developer tooling and are not linked into the runtime.
- Shared build scripts select parallel jobs from `BUILD_JOBS`, then
  `CMAKE_BUILD_PARALLEL_LEVEL`, then the CPU count, rejecting invalid values.
  CI caps compilation at two jobs; tests remain sequential.

Static analysis follow-up:

- `scripts/check.sh` now requires Cppcheck 2.13+ and runs the same static
  analysis gate on local verification, pull requests, pushes, and version tags.
- `scripts/static-analysis.sh` configures a CMake compile database, then analyzes
  all 37 configured runtime/test translation units and their project headers in
  parallel. Errors, warnings, performance and portability findings fail checks.
- Cppcheck 2.19 and the Ubuntu 24.04 Cppcheck 2.13 package both passed locally
  without project findings. Dependency diagnostics alone are suppressed;
  analyzer-only Catch2 macro definitions retain assertion expressions.
- Eight build-script tests cover parallel job selection, analyzer invocation,
  missing-tool handling, log retention and failure propagation. A real analyzer
  regression checks that a deliberate null-pointer defect fails the gate.
- Logs are saved under the build directory and uploaded by verification CI.
  Cppcheck is system-provided developer tooling and is not bundled or linked
  into the runtime.

The original milestone checkmarks describe modules being present. They should
not be read as proof that every v0 acceptance criterion is complete. Continue
with these remaining items before calling the release finished.


Trivy security follow-up (2026-10-10):

- Added checksum-pinned Trivy 0.75.0 installation and a required security gate
  to local verification, pull requests, master pushes and version-tag builds.
  Weekly/manual verification refreshes advisory evidence without code changes.
- Explicit inventories cover both CMake FetchContent pins, the optional Python
  coverage requirement, and distro packages owning the trusted built executables'
  dynamic-library closure. Completeness checks reject missing packages, unknown
  owners and unsupported/EOL OS detection rather than accepting empty scans.
- Private JSON/log reports remain under the build directory, with restrictive
  permissions; details and secret matches are not printed or uploaded publicly.
  License detection is informational inventory. No findings are suppressed and
  all security severities, including unfixed findings, fail verification.
- Local GCC Release verification passed 103 C++ tests, eight build-script tests,
  loopback protocol checks and Cppcheck. Ten security-script/inventory tests
  passed, including real Trivy rejection of a synthetic private-key fixture.
- The full Trivy run scanned two C++ packages, the coverage requirement and 29
  linked distro packages on Ubuntu 26.04. Repository/dependency and license
  scans completed without security findings. The runtime package scan reported
  four findings with no fixed version listed in the current advisory database;
  therefore `scripts/check.sh` exits nonzero. Details are retained privately in
  `<build>/security/runtime.json`, not in this public document.
- This is a working, failing security gate rather than a clean security bill.
  Resolve or reassess runtime findings with updated vendor advisory/package
  evidence. CMake source scanning and a manually generated SBOM cannot prove
  complete advisory coverage; see `docs/security-checks.md` for scope and limits.
