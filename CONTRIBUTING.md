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

```bash
# 1. Branch
git checkout -b feat/short-description

# 2. Make your changes, then verify
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
python3 scripts/check_protocol.py build
# Format changed C++ files using the repository .clang-format.
clang-format -i path/to/changed_file.cpp

# 3. Commit using Conventional Commits
git commit -m "feat(scope): what changed"

# 4. Push and open a PR
```

## Pull request checklist

- [ ] Build (warnings treated as errors), regression tests, and protocol smoke tests pass locally.
- [ ] New behavior has tests; bug fixes have a regression test.
- [ ] Public API changes are documented in code and (if breaking) in
      `CHANGELOG.md`.
- [ ] Commit messages follow [Conventional Commits](https://www.conventionalcommits.org/).
- [ ] PR description explains *what* and *why*, with screenshots/logs for
      behavioral changes.
- [ ] No secrets, credentials, or generated artifacts committed.

## Code review

Reviewers will check correctness, tests, security, and adherence to
`AGENTS.md`. Expect requests for clarification on naming, error handling, and
API shape — those are usually the highest-leverage things to get right.

## Working with AI agents

If you're prompting a coding agent (Claude Code, Codex, OpenCode, Cursor,
Windsurf, Gemini CLI, etc.) for this repo:

- The agent will pick up `AGENTS.md` automatically (directly or via the
  tool-specific shim).
- Give it a focused task with explicit acceptance criteria.
- Review its diffs the same way you'd review a human PR — never auto-merge.
- If the agent suggests changes outside the section it was asked to touch,
  push back unless the rationale is clear.
