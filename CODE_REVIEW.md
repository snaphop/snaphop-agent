# Code review

Review commits, branches, PRs, and working-tree changes against
[AGENTS.md](AGENTS.md), [SECURITY.md](SECURITY.md), the current
[contracts](docs/cli.md), nearby implementation and tests, and relevant ADRs.
For whole-queue reviews, use the [repository skills](CONTRIBUTING.md#repository-review-skills).

## Process

1. Establish the target and acceptance criteria. Use an independent reviewer
   when one is available and delegation is authorized; otherwise perform the
   review directly and state its limits.
2. Review correctness, regressions, security, failure handling, compatibility,
   concurrency, resource bounds, observability, and meaningful test coverage.
3. Report each finding with severity, precise `file:line`, trigger conditions,
   concrete failure mode, and why existing safeguards or tests do not cover it.
4. Give an overall verdict and verification gaps. Say explicitly when there
   are no findings; do not invent stylistic defects.

Review authority alone does not authorize edits or external actions. Honor
fixes, publication, merge, or cleanup already authorized in the session.

## Review priorities

- Treat model output as untrusted. Check schemas, unknown tools, deny precedence,
  dangerous flags, overwrite approval, and workspace path containment.
- Check symlinks and filesystem error paths. Current resolve-then-open access
  is not race resistant; do not claim a full sandbox.
- Trace native tool IDs and call/result pairing through continuation and replay.
  Interrupted tools must not be silently rerun. Check transcript append,
  truncated-tail recovery, bounded loading, and atomic compaction.
- Verify per-session serialization, global concurrency, bounded queues/workers,
  deadlines, cancellation, shell process groups, and output truncation.
- Check RAII ownership, arena lifetimes, bounded prompts/transcripts/event
  buffers, and stable memory across repeated turns.
- Keep API keys and other secrets out of transcripts, diagnostics, fixtures,
  and tool results. Check provider HTTP failures and response bounds.
- Keep CLI/help/config/JSONL, examples, ADRs, notices, and changelog synchronized.
  Check dependency necessity, pinned versions/actions, static analysis, and
  warnings-as-errors. Prefer mock and loopback tests for local verification.
- Report coverage and performance observations accurately. Passing local fake
  provider tests is not live-provider verification; buffered responses are not
  SSE streaming. See [remaining gaps](docs/status.md).

## Severity

| Severity | Meaning |
|---|---|
| Critical | Likely credential/data compromise, destructive behavior, or broad outage. |
| High | Material correctness or security failure in a common reachable path. |
| Medium | Real failure with narrower preconditions. |
| Low | Limited impact worth correcting. |
| Nit | Optional clarity or style improvement without meaningful behavior impact. |

## Acting on findings

Fix substantive findings within the authorized scope. Respect deliberate design
choices on minor style issues. If a defect is repeated in mirrored code, correct
all affected occurrences or explain the narrower scope. Add meaningful regression
coverage for behavior fixes. Report which findings were fixed, declined, or
deferred and why. Run `scripts/check.sh` before handing off changes and state
any verification gaps. Keep suspected vulnerabilities private.
