## What Changed

<!-- Describe the resulting behavior in a sentence or two. -->

## Why

<!-- Explain the problem and link the issue, if there is one. -->

## How It Was Verified

```text
<!-- Commands run and their results. scripts/check.sh is the minimum check. -->
```

Not verified: <!-- State any gaps explicitly. -->

## Operational And Compatibility Impact

<!-- Configuration, protocol, dependencies, persisted state, and upgrade impact. -->

## Checklist

- [ ] `scripts/check.sh` passes (build, script and C++ tests, local HTTP protocol checks, static analysis).
- [ ] Changed C++ files follow `.clang-format`; new behavior has meaningful tests.
- [ ] Public behavior and configuration changes are documented; notable changes appear in `CHANGELOG.md`.
- [ ] Significant architectural decisions have an ADR.
- [ ] Workspace boundaries, resource limits, tool policy, and transcript replay remain safe.
- [ ] No credentials, private data, or generated build artifacts are included.
- [ ] Review follows `CODE_REVIEW.md`; verification gaps are explicit.
