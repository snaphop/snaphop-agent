# Trivy security checks

SnapHop Agent requires Trivy checks in `scripts/check.sh` and the verification
and tag-release workflows. Verification also runs weekly and can be dispatched
manually so newly published advisories are checked without source changes.

```bash
scripts/install-trivy.sh
BUILD_JOBS=2 scripts/check.sh
# After building, rerun just security checks:
scripts/security-check.sh build
```

The installer downloads Trivy 0.75.0 for Linux x86_64, verifies its committed
SHA256, and retains the upstream LICENSE under `.tools/trivy/`. It executes no
remote installation script. Use `TRIVY=/path/to/trivy` for another installation.
Python 3, `ldd`, and `dpkg-query` are needed to inventory runtime packages; this
part of the check currently supports maintained Debian/Ubuntu systems. Trivy
downloads and updates advisory databases and misconfiguration checks; network
failures fail verification rather than produce a successful offline result.

## Coverage and gates

| Scan | Scope and behavior |
|---|---|
| Repository | Vulnerabilities in supported manifests, including the optional coverage requirements; secrets and supported IaC misconfiguration checks in authored source, docs, tests, scripts, skills and `.github`. |
| FetchContent | Generated CycloneDX SBOM for nlohmann/json and Catch2, using actual CMake versions and archive hashes with Conan package identities. No Conan installation or build-system migration is required. |
| Runtime packages | `ldd` resolves the trusted built `agent_cli` and `agentd` executables' library closure. `dpkg-query` supplies owner/version/source metadata. Only these package records and distro release metadata are staged for an OS vulnerability scan, including libcurl and linked TLS libraries. |
| Licenses | Full source/text license detection plus declared C++ licenses in the SBOM. Informational inventory; permitted licenses do not fail a security check merely for being detected. Review usage and notices manually. |

All scans include UNKNOWN, LOW, MEDIUM, HIGH and CRITICAL findings. The full JSON
reports are evaluated separately from scanner execution: HIGH/CRITICAL
vulnerabilities block even when no fix is listed. UNKNOWN/LOW/MEDIUM
vulnerabilities remain in reports and sanitized aggregate counts without blocking.
Secrets and misconfigurations block at every severity. No unfixed vulnerabilities
are hidden. Missing tools, inventory failures, scanner errors, malformed/missing
reports, unsupported/EOL OS detection and omitted inventoried packages also fail.
Every scan is attempted after inventory succeeds, even when an earlier scan
finds an issue. The check does not read `.trivyignore`; suppressions must not be
added to hide unresolved findings. Tests include real synthetic-secret detection
and incomplete inventory rejection.

The script excludes `.git`, generated build directories, scanner installations,
virtual environments, release outputs, `.agent`, `workspace`, and operator
`agent.json`. These contain generated artifacts or operator data. Authored code
is scanned, and pinned/linked dependencies are checked through explicit
inventories rather than scanning all downloaded third-party source trees.

## Private evidence

Reports live under `<build>/security/`, with directory mode 0700 and new files
mode 0600. Each run discards old scan JSON before scanning. They include
`repository.json`, `dependencies.json`, `runtime.json`, `licenses.json`, per-scan
logs, the C++ SBOM and the staged runtime package inventory. The script prints scanner version and generic scan status. Failed scans also
print aggregate severity counts, fix-availability counts, and counts of secrets
and misconfigurations. Summaries contain no package names, advisory IDs, paths,
secret matches or report-controlled text. A missing/invalid report is identified
as an operational diagnostic gap, with raw details left in the private log. CI does not upload raw security artifacts to this public
repository. Investigate CI failures locally on the same distro/toolchain.

Follow [SECURITY.md](../SECURITY.md): report suspected vulnerabilities privately
to security@snaphop.com, redact credentials, and never post secret matches,
unredacted reports or exploit details in public issues/PRs. Report failures
honestly rather than describing a completed scan as a clean scan.

## Limits

Trivy checks known advisories, recognized secret patterns, supported IaC types
and license classifications. No supported IaC files may exist in this repository;
that does not establish the safety of every CI workflow or config setting.
The repository scan does not cover Git history or arbitrary external operator
workspaces. The generated SBOM is an explicit inventory, not proof of perfect
advisory coverage. Distro metadata accounts for vendor versioning/backports;
it cannot establish the security of unowned or manually installed libraries,
which cause inventory failure. Dynamic `dlopen` plugins, other host services,
compiler security, and live-provider configuration are outside this inventory.

These checks complement Cppcheck, tests, sanitizers and manual review. Known
runtime acceptance/security gaps remain in [status.md](status.md); passing Trivy
cannot establish a full sandbox or prove there are no vulnerabilities.

References: [Trivy C/C++ coverage](https://trivy.dev/docs/latest/coverage/language/c/),
[filesystem scanning](https://trivy.dev/docs/latest/target/filesystem/),
[SBOM scanning](https://trivy.dev/docs/latest/target/sbom/), and
[license scanning](https://trivy.dev/docs/latest/scanner/license/).
