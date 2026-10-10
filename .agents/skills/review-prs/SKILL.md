---
name: review-prs
description: Review and resolve the repository's complete open pull-request queue, including dependency updates. Use when asked to review, test, merge, or clean up all outstanding PRs; do not use for one ordinary PR unless explicitly invoked.
---

# Review Pull Requests

## Establish scope and authority

Read [AGENTS.md](../../../AGENTS.md), [SECURITY.md](../../../SECURITY.md),
[CONTRIBUTING.md](../../../CONTRIBUTING.md), [CODE_REVIEW.md](../../../CODE_REVIEW.md),
and the relevant [documentation](../../../docs/README.md).
Use the repository remote to select the issue host; the canonical repository is
`snaphop/snaphop-agent` and its default branch is `master`.
Honor actions already authorized in the session. Reviewing does not itself
authorize tracker comments or closures, commits, pushes, merges, branch deletion,
history rewrites, releases, or paid provider calls. Do authorized work without
asking again. Keep suspected vulnerabilities private under `SECURITY.md`.
Preserve pre-existing user changes and remove only temporary artifacts you created.

## Inventory and review

Inventory all open PRs, including Dependabot, and record the initial set.
Read each description, full diff, discussion, linked issues, acceptance criteria,
and author rationale. Use the finding format in `CODE_REVIEW.md`. Prioritize:

- Workspace containment, symlink handling, dangerous-tool and overwrite policy.
- Typed argument validation, tool IDs and native call/result pairing, replay
  after interruption, transcript recovery and atomic compaction.
- Per-session serialization, global limits, bounded workers and queues,
  cancellation, shell timeout/process-group cleanup, and bounded outputs.
- Ownership and object lifetimes, turn arenas, bounded context/event buffers,
  HTTP response limits, and secrets excluded from output and persistence.
- Provider separation, CLI/help/config/JSONL compatibility, docs and ADR sync.
- Dependency necessity, pinned versions/actions and notices, warnings, static
  analysis, meaningful regressions, coverage and performance impact.

Check mergeability with `master`. Resolve conflicts when authorized, preserving
security and compatibility invariants. Merge the base into a shared PR branch;
rewrite shared history only with explicit authority. Apply target `AGENTS.md`
rules rather than importing approval requirements from another repository.

## Verify and resolve

Run focused checks, then `scripts/check.sh` and `git diff --check`. Exercise
coverage or sanitizer/performance checks when relevant. Local mock and loopback
tests do not prove live-provider account access or true streaming. Record those
gaps. Never bypass failed checks or invent successful validation.

Fix findings within the task's authority. Merge only with existing merge authority
after required verification and CI pass for the actual PR head. Recheck the head
before merging, verify GitHub's result and updated `master`, and report the merge
commit. Merging is separate from publishing: version tags trigger the release
workflow. Use cleanup authority to remove feature branches and temporary
worktrees without disturbing pre-existing work.

## Reconcile

Recheck the open queue. For every initial PR, report ID/title/branch, findings
and resolution, compatibility impact, conflict status, verification and final
state. Explain any remaining open or blocked PR and any newly arrived items.
