# AGENTS.md

> **Canonical instruction file for AI coding agents working in this repo.**
> Follows the [AGENTS.md](https://agents.md) convention adopted by OpenAI
> Codex, Claude Code (as a fallback to `CLAUDE.md`), GitHub Copilot, Cursor,
> Windsurf, OpenCode, Aider, Sourcegraph Amp, and others.
>
> Tool-specific files (`CLAUDE.md`, `GEMINI.md`, `.cursor/rules/`,
> `.windsurf/rules/`, `.github/copilot-instructions.md`) are configured to
> defer to this file. **If anything conflicts, this file wins.** Update here
> and the rules propagate everywhere.

---

## 1. Project overview

<!-- TODO: replace this section -->
- **Name:** my-project
- **What it does:** one-sentence description.
- **Primary language / runtime:** e.g. TypeScript on Node 20, Python 3.12, Go 1.22.
- **Key dependencies / frameworks:** e.g. Next.js, FastAPI, gRPC.
- **Deployment target:** e.g. AWS Lambda, Cloudflare Workers, Docker on Fly.io.

## 2. Repository layout

```
.
├── src/        # application source
├── tests/      # automated tests (mirror src/ structure)
├── scripts/    # one-off and CI helper scripts
├── docs/       # human documentation, architecture notes, ADRs
├── .github/    # CI, issue/PR templates, Copilot instructions
└── AGENTS.md   # ← you are here
```

Add new modules under `src/<domain>/`. Tests live in `tests/<domain>/` and
mirror the source path.

## 3. Setup

```bash
# 1. Clone and enter
git clone <repo> && cd <repo>

# 2. Install dependencies   ← TODO: replace with the right command
# npm install
# pnpm install
# uv sync
# poetry install
# go mod download
# cargo build

# 3. Copy env template
cp .env.example .env
```

## 4. Common commands

Agents should prefer these scripts over invoking tools directly, so behavior
stays consistent across local dev, CI, and agent runs.

| Task          | Command                          |
|---------------|----------------------------------|
| Build         | `<TODO: build cmd>`              |
| Run (dev)     | `<TODO: dev cmd>`                |
| Run (prod)    | `<TODO: start cmd>`              |
| Test (all)    | `<TODO: test cmd>`               |
| Test (single) | `<TODO: e.g. pytest path::name>` |
| Lint          | `<TODO: lint cmd>`               |
| Format        | `<TODO: format cmd>`             |
| Typecheck     | `<TODO: typecheck cmd>`          |

**Always run lint, format, and typecheck before declaring a task complete.**

## 5. Code style

- Keep functions small and single-purpose. Prefer pure functions; isolate I/O.
- Match the surrounding style of any file you edit. Don't reformat unrelated
  code.
- Names: descriptive over clever. Avoid abbreviations except common ones
  (`id`, `url`, `db`).
- Comments explain **why**, not **what**. Code shows what.
- No dead code, no commented-out blocks left behind, no `console.log` /
  `print` debugging in committed code.
- Errors: fail loudly at the boundary, fail fast inside. Don't swallow
  exceptions silently.
- Public APIs (anything exported) get docstrings / JSDoc / equivalent.

## 6. Testing

- Write or update tests for every behavior change. A bug fix without a
  regression test is incomplete.
- Test names describe the behavior, not the implementation:
  `it("rejects expired tokens")` ✓ — `it("calls verifyJwt")` ✗.
- Use real values over mocks where practical. Mock at the network and
  filesystem boundary, not inside your own modules.
- Don't disable, skip, or weaken existing tests to make a change pass — fix
  the change instead. If a test is genuinely wrong, update it deliberately
  and explain why in the PR.

## 7. Git, commits, and PRs

- **Branches:** `feat/<short-desc>`, `fix/<short-desc>`, `chore/<short-desc>`,
  `docs/<short-desc>`.
- **Commits:** [Conventional Commits](https://www.conventionalcommits.org/).
  - `feat: add OAuth refresh-token rotation`
  - `fix(auth): reject tokens past their nbf claim`
  - `chore(deps): bump axios to 1.7.4`
- **PRs** must include:
  - What changed and why (link the issue).
  - How it was tested (commands run, edge cases covered).
  - Screenshots / logs for UI or behavior changes.
- Keep PRs small and reviewable. Split refactors from feature changes.

## 8. Security & secrets

- **Never** commit `.env`, credentials, API keys, or customer data. The
  `.gitignore` excludes them — don't override it.
- Use `.env.example` to document required variables; leave values blank or
  use obvious placeholders (`REPLACE_ME`).
- Validate and sanitize all external input. Treat data from network, disk,
  and environment as untrusted.
- Don't log secrets, tokens, full request bodies, or PII. Redact at the
  logger boundary.
- Report vulnerabilities per `SECURITY.md`.

## 9. Things agents should NOT do without explicit approval

- Force-push, rewrite history on shared branches, or delete branches.
- Add new top-level dependencies (run a small experiment in `scripts/` or
  flag it for review first).
- Change the public API, the database schema, or any file under `infra/` or
  `.github/workflows/`.
- Disable, weaken, or skip tests, lints, type checks, or pre-commit hooks.
- Generate large amounts of boilerplate or speculative abstraction "for
  later." Build for what's needed now.
- Touch `LICENSE`, `SECURITY.md`, or `CODE_OF_CONDUCT.md`.

## 10. When in doubt

1. Read existing code in the same area — match its patterns.
2. Check `docs/` and `docs/decisions/` for prior context.
3. Ask a clarifying question rather than guessing on irreversible decisions
   (schema, API shape, auth model).
4. Prefer the smallest change that solves the problem.
