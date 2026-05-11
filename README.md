# my-project

> One-line description of the project. <!-- TODO -->

A cross-agent software project template centered on **`AGENTS.md`**, with
compatibility shims for Claude Code, Gemini CLI, GitHub Copilot, Cursor, and
Windsurf.

## Quick start

```bash
# Install
<TODO: install command>

# Configure
cp .env.example .env

# Run
<TODO: dev command>
```

## Documentation

- [`AGENTS.md`](./AGENTS.md) — canonical instructions for AI coding agents
  (and a good human onboarding doc: setup, commands, conventions, what not
  to do).
- [`docs/architecture.md`](./docs/architecture.md) — system design overview.
- [`docs/decisions/`](./docs/decisions/) — Architecture Decision Records.
- [`CONTRIBUTING.md`](./CONTRIBUTING.md) — how to propose changes.
- [`SECURITY.md`](./SECURITY.md) — how to report vulnerabilities.
- [`CHANGELOG.md`](./CHANGELOG.md) — release history.

## Project structure

```
.
├── src/        # application source
├── tests/      # automated tests
├── scripts/    # one-off and CI helper scripts
├── docs/       # architecture, ADRs, runbooks
└── .github/    # CI, templates, agent instructions
```

## Agent files

| File                                | Tool                              | Status   |
|-------------------------------------|-----------------------------------|----------|
| `AGENTS.md`                         | Codex, OpenCode, Aider, Amp, Roo, Continue, Copilot, Windsurf, Claude Code (fallback) | **canonical** |
| `CLAUDE.md`                         | Claude Code (imports `@AGENTS.md`)| active   |
| `GEMINI.md`                         | Gemini CLI                        | active   |
| `.cursor/rules/project.mdc`         | Cursor (modern)                   | active   |
| `.windsurf/rules/project.md`        | Windsurf (modern)                 | active   |
| `.github/copilot-instructions.md`   | GitHub Copilot                    | active   |
| `.cursorrules`                      | Cursor (legacy)                   | shim     |
| `.windsurfrules`                    | Windsurf (legacy)                 | shim     |

`AGENTS.md` is the source of truth. Every other file defers to it.

## Contributing

See [`CONTRIBUTING.md`](./CONTRIBUTING.md). Run lint, typecheck, and tests
before opening a PR.

## License

[MIT](./LICENSE)
