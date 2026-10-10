# Security policy

## Reporting a vulnerability

Please **do not** open a public issue, pull request, or discussion for suspected
security vulnerabilities. This repository is public.

Instead, email <security@snaphop.com> with:

- A description of the issue and its impact.
- Steps to reproduce, or a proof of concept.
- Affected versions, if known.
- Your name/handle for credit (optional).

You should receive a response within 3 business days. We'll work with you on
a coordinated disclosure timeline — typically 90 days from confirmation, or
sooner if a fix ships earlier.

## Supported versions

| Version | Supported |
|---------|-----------|
| Current `master` and latest release | ✅ |
| Older releases | ❌ |

Do not include real credentials, private workspace files, or production logs.
Use synthetic examples and redact sensitive values. Known pre-1.0 limitations
are tracked in [`docs/status.md`](./docs/status.md), including filesystem races,
shell secret inheritance, and incomplete cancellation/streaming behavior.

## Scope

In scope: vulnerabilities in this repository's code and its direct
dependencies' usage.

Out of scope: third-party services, infrastructure issues unrelated to this
codebase, social engineering, denial-of-service via volumetric traffic.
