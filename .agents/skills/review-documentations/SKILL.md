---
name: review-documentations
description: Audit and correct all repository documentation against current implementation, configuration, tests, and workflows. Use for a complete documentation review; do not use for a narrow prose edit unless explicitly invoked.
---

# Review Documentation

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

## Inventory the contracts

Inventory root Markdown, `docs/`, repository skills and agent shims, examples,
PR templates, CI/release workflows, dependency configuration, scripts, and notices.
Read CMake, public headers, entry points, config parsing, nearby implementation,
and tests as the primary evidence. Compare CLI/help, JSONL events, transcript
records, model adapters, tool schemas and policy, limits, and local checks.

Read ADRs as recorded intent. Use `docs/status.md` to distinguish delivered
behavior, measured observations, and outstanding acceptance criteria. Milestone
checkmarks alone do not prove completion. In particular, buffered output is not
SSE streaming and current coverage floors are not 100% coverage.

## Correct and validate

Fix stale names, commands, defaults, paths, versions, links, unsupported promises,
and contradictory instructions. Preserve historical release notes and decision
records; explain superseding behavior rather than rewriting history.
Do not implement missing runtime features merely to make a document true.
Keep examples free of credentials and use placeholders where account-specific
model settings are required. Check LICENSE/NOTICE when dependencies change.

Validate relative links on a case-sensitive filesystem, skill metadata, Claude
symlinks, command examples, and agreement between config examples and source.
Verify external references when their accuracy or freshness matters, using
primary sources. Run `scripts/check.sh` and `git diff --check`; state unsupported
platforms and any examples that could not be exercised.

## Report

Summarize corrected contracts, documents changed, validation, and evidence gaps.
Do not describe an unpublished change as released or unverified performance as
an established guarantee.
