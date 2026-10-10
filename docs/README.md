# Documentation

- [CLI and JSONL contract](cli.md) — entry points, configuration, events, limits and persistence.
- [Local development setup](host-setup.md) — build, test, static analysis, coverage and release commands.
- [Architecture](architecture.md) — runtime and component overview.
- [Security checks](security-checks.md) — Trivy coverage, private reports and limitations.
- [Tests and fixtures](../tests/README.md) — local test scope and fixture provenance.
- [Maintenance status](status.md) — measured verification and remaining v0 acceptance gaps.
- [Repository comparison](repository-parity.md) — skills/document parity with snaphop-agent-vm.
- [GitHub migration](github-migration.md) — repository migration record.
- [ADR 0001](decisions/0001-record-architecture-decisions.md) — recording architecture decisions.
- [ADR 0002](decisions/0002-bound-tool-execution-and-preserve-replay-evidence.md) — bounded tool execution and replay evidence.

Contributor policies live in [AGENTS.md](../AGENTS.md),
[CONTRIBUTING.md](../CONTRIBUTING.md), [CODE_REVIEW.md](../CODE_REVIEW.md), and
[SECURITY.md](../SECURITY.md). Whole-repository review workflows are canonical
under [`.agents/skills/`](../.agents/skills/), with Claude discovery symlinks under
[`.claude/skills/`](../.claude/skills/). See
[the skill catalog](../CONTRIBUTING.md#repository-review-skills) for invocation.
