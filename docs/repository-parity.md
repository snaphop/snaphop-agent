# Agent VM repository comparison

Compared on 2026-10-10 against `snaphop/snaphop-agent-vm` master commit
`3165aaf3a5064c88cbdd81cacfbfd536fabea6b4`. The VM repository is a Go CLI for
QEMU/libvirt provisioning; this repository is a C++ headless agent runtime.
Parity means equivalent contributor and agent workflows adapted to this
project's contracts. It does not import VM functionality or its approval rules.

| Reference material | snaphop-agent equivalent / result |
|---|---|
| Four `.agents/skills/*/SKILL.md` workflows | Added `review-bugs`, `review-documentations`, `review-enhancements`, `review-prs`, covering the same whole-queue/audit tasks. |
| Each skill's `agents/openai.yaml` | Added discovery metadata and invocation prompts. |
| Four `.claude/skills/*` links | Added relative symlinks to the canonical skills; no duplicate skill content. |
| `CODE_REVIEW.md` | Added severity, evidence, finding format, runtime-specific priorities and resolution guidance. |
| `docs/cli.md` | Added current CLI, JSONL, configuration, credentials, limits and persistence contract. |
| `docs/host-setup.md` | Added local C++ build/test/static-analysis/coverage/release setup runbook. |
| `AGENTS.md`, `CLAUDE.md`, `GEMINI.md` | Existing canonical instructions and tool shims retained; review workflow discovery added. |
| README, CONTRIBUTING, SECURITY, CHANGELOG | Existing equivalents retained; contributor/review links synchronized. |
| `docs/README.md`, architecture and decisions | Documentation index expanded; existing runtime architecture and two ADRs retained. |
| PR template, verification/release workflows, Dependabot | Existing equivalents retained; template updated for current checks and review policy. |
| LICENSE, NOTICE | Existing project licensing and dependency notices retained. |
| `test/toolout/README.md` | Added [tests/README.md](../tests/README.md) for local mocks, protocol fixtures, provenance and verification limits. VM host-tool capture tables do not apply. |
| VM-specific ADRs 0002–0018 | Not copied: hypervisor, image and guest provisioning decisions do not describe this runtime. Runtime ADRs document their own contracts. |

Skill bodies replace Go/make, hypervisor mutation, guest boot tests, and VM
ownership with CMake/scripts, mock providers, workspace/tool safety, transcript
replay, runtime bounds and local protocol checks. Existing authorization is
respected; importing these workflows does not authorize issue/PR mutations.

Full coverage, true streaming, filesystem race hardening, and other acceptance
gaps remain tracked in [status.md](status.md); document parity does not claim
those features complete. Recheck the reference inventory when updating these
workflows rather than blindly copying future domain-specific additions.
