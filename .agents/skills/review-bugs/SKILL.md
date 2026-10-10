---
name: review-bugs
description: Review the repository's complete open bug queue, reproduce reports, and resolve valid bugs within existing authority. Use for an all-bugs review; do not use for a single defect unless explicitly invoked.
---

# Review Bugs

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

## Inventory and classify

Inventory every open bug using the host's live issue types and labels, including
Bug types and bug labels where present. Do not infer classification from titles.
Record the initial issue IDs and recheck the queue before finishing.
Read each report, discussion, reproduction, affected version, linked PR, and
relevant history. Trace the owning runtime, provider, transcript, session, or tool
module and its tests. Reproduce using mock `IModelClient`, temporary workspaces,
and loopback HTTP fixtures where possible; no provider credentials are required.

Classify each report as valid, invalid, or blocked with concrete evidence.
Distinguish current source behavior from behavior in a published release.
For invalid reports, explain the supported contract and correction. For blocked
reports, name the missing reproduction or evidence; do not guess.

## Fix and verify

Fix valid reports when authorized, keeping the change focused. Preserve workspace
containment, default-deny dangerous tools, overwrite approval, bounded allocations,
call/result pairing, session serialization, cancellation, and secret redaction.
Add a regression that fails before the fix and passes afterward. Exercise error,
boundary, and recovery cases when relevant. Synchronize CLI/config/protocol docs,
ADRs, and notable changelog entries.

Run focused tests, then `scripts/check.sh` and `git diff --check`. Use coverage,
sanitizers, and performance observations when they substantiate the change.
Never weaken gates to resolve a failure. Missing tools or untested live-provider
behavior are verification gaps, not successful checks.

## Reconcile

For every initial issue, report ID, classification, evidence, fix or explanation,
verification, and tracker/release state. Close or comment only within existing
authority and after checking the current issue state. A local fix or merged PR
does not establish that a release containing it is available.
