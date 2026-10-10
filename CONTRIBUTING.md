# Contributing

Thanks for contributing! This project is set up to be worked on by both humans
and AI coding agents. The conventions are the same for both.

## Before you start

1. Read [`AGENTS.md`](./AGENTS.md) — it covers setup, commands, code style,
   testing, git conventions, and what not to touch.
2. Read recent ADRs in [`docs/decisions/`](./docs/decisions/) to understand
   architectural context.
3. For non-trivial changes, open an issue first to discuss the approach.

## Development workflow

Fork [snaphop/snaphop-agent](https://github.com/snaphop/snaphop-agent) on GitHub
and submit pull requests there. Bug reports and feature requests belong in
its GitHub issue tracker; suspected vulnerabilities go to <security@snaphop.com>.

```bash
# 1. Branch
git checkout -b feat/short-description

# 2. Make your changes, then verify
scripts/check.sh
# Format changed C++ files using the repository .clang-format.
clang-format -i path/to/changed_file.cpp

# 3. Commit using Conventional Commits
git commit -m "feat(scope): what changed"

# 4. Push and open a PR
git push --set-upstream origin feat/short-description
```

GitHub Actions runs the same check on pull requests and pushes to `master`.
Install Cppcheck 2.13+ before running `scripts/check.sh`; static analysis is a
required part of verification. Install Trivy with `scripts/install-trivy.sh`;
the [security gate](docs/security-checks.md) is also required and runs locally
and in CI. Keep finding details private, never weaken gates or add broad ignores,
and disclose verification failures explicitly. Run `scripts/static-analysis.sh`
for Cppcheck alone; its log stays under the build directory. Project diagnostic suppressions
must be narrowly scoped and explain why the finding is a false positive.
It also runs `scripts/coverage.sh` with the optional tooling in
`scripts/requirements-coverage.txt`; see the README for local setup and coverage
thresholds. Keep uncovered paths visible in reports when expanding tests.
Releases are published only on `vX.Y.Z` tags, after verification. Maintainers
can build the runtime archive locally with
`VERSION=v0.1.0 scripts/build-release.sh`; this does not publish it.
Keep dependency notices in `NOTICE` synchronized with dependency changes.

## Pull request checklist

- [ ] `scripts/check.sh` passes: warnings-as-errors build, script and C++ tests, local protocol checks, static analysis, and Trivy security scans.
- [ ] New behavior has tests; bug fixes have a regression test.
- [ ] Public API changes are documented in code and (if breaking) in
      `CHANGELOG.md`.
- [ ] Commit messages follow [Conventional Commits](https://www.conventionalcommits.org/).
- [ ] PR description explains *what* and *why*, with screenshots/logs for
      behavioral changes.
- [ ] No secrets, credentials, or generated artifacts committed.

## Code review

Follow [CODE_REVIEW.md](CODE_REVIEW.md) for evidence, severity, runtime safety
priorities, and resolution. State what was verified and any gaps. Keep the
[CLI contract](docs/cli.md), examples, help, ADRs, and changelog synchronized.
See [local setup](docs/host-setup.md) for tool installation and checks.

## Working with AI agents

If you're prompting a coding agent (Claude Code, Codex, OpenCode, Cursor,
Windsurf, Gemini CLI, etc.) for this repo:

- The agent will pick up `AGENTS.md` automatically (directly or via the
  tool-specific shim).
- Give it a focused task with explicit acceptance criteria.
- Review its diffs the same way you'd review a human PR — never auto-merge.
- If the agent suggests changes outside the section it was asked to touch,
  push back unless the rationale is clear.


## Repository review skills

Canonical skills live under `.agents/skills/`; `.claude/skills/` contains relative
symlinks to those folders. Each skill includes `agents/openai.yaml` metadata.
Use the same workflow from supported agents without maintaining duplicate copies.

| Invocation | Scope |
|---|---|
| [`$review-bugs`](.agents/skills/review-bugs/SKILL.md) | Entire open bug queue: reproduce, classify, and fix when authorized. |
| [`$review-documentations`](.agents/skills/review-documentations/SKILL.md) | Complete documentation audit against source, tests, config, and workflows. |
| [`$review-enhancements`](.agents/skills/review-enhancements/SKILL.md) | Entire enhancement queue: classify without implementation. |
| [`$review-prs`](.agents/skills/review-prs/SKILL.md) | Entire open PR queue, including dependency updates; resolve, merge and clean up within existing authority. |

These workflows preserve session authorization and private security reporting.
A request for review alone does not authorize external tracker mutations or
publication. Focused tasks do not require a whole-queue review. The
[comparison record](docs/repository-parity.md) explains adaptations from the VM
repository and domain-specific documents that were not copied.
