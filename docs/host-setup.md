# Local development setup

This is the C++ runtime's host setup runbook. It requires no hypervisor, guest VM,
frontend, provider account, or paid API calls for the automated test suite.
Linux is the verified platform. Build scripts use Bash and POSIX process tools.

## Prerequisites

- CMake 3.20+, GCC 13+ or Clang 16+ with a C++23 standard library supporting
  `std::expected`, and libcurl development headers.
- Python 3 for script and local HTTP checks, Cppcheck 2.13+ for the required
  static-analysis gate, Trivy 0.75.0 for security checks, and clang-format for changed C++ files.
- Network access on first configure to fetch pinned nlohmann/json and Catch2.
  Catch2 is omitted for `BUILD_TESTING=OFF`. Cached dependencies can be reused.

The CI toolchain on Ubuntu 24.04 can be installed with:

```bash
sudo apt-get update
sudo apt-get install -y gcc-14 g++-14 cmake libcurl4-openssl-dev python3 python3-venv cppcheck clang-format
export CC=gcc-14 CXX=g++-14
scripts/install-trivy.sh
BUILD_JOBS=2 scripts/check.sh
```

Run from the repository root. The check builds Release with warnings as errors,
runs build-script tests, C++ tests, local loopback HTTP/protocol checks, and
Cppcheck, and the Trivy security gate. It needs no credentials. Compiler concurrency uses `BUILD_JOBS`, then
`CMAKE_BUILD_PARALLEL_LEVEL`, then CPU count; lower it if memory is limited.
Tests remain sequential. Logs for analysis are under
`build/static-analysis/cppcheck.log`. Run `scripts/static-analysis.sh` separately
or choose an executable with `CPPCHECK=/path/to/cppcheck`.

Security checks additionally need `curl`, `sha256sum`, `tar`, `ldd`, and
`dpkg-query`. The complete runtime package check currently supports maintained
Debian/Ubuntu hosts. The installer downloads a checksum-pinned Linux x86_64
archive; other scanner platforms need a manual Trivy install. Database and
check-bundle downloads require network access. Read
[security checks](security-checks.md) before sharing scan results.

## Coverage and performance

```bash
python3 -m venv .venv-coverage
.venv-coverage/bin/python -m pip install -r scripts/requirements-coverage.txt
CC=gcc-14 CXX=g++-14 GCOV=gcov-14 PYTHON=.venv-coverage/bin/python \
  BUILD_JOBS=2 scripts/coverage.sh
build/agent_tests '[performance]'
```

Coverage needs GCC and matching gcov. Reports are in `build-coverage/coverage/`;
CI floors are 92% lines and 80% branches. Set `COVERAGE_MIN_LINES=100` and
`COVERAGE_MIN_BRANCHES=100` to enforce the requested full coverage target.
[Status](status.md) records the remaining gap. Performance tests use mock models
and report observations, not live-provider latency guarantees.

## Manual runtime use

Copy [the example config](../examples/config.example.json) to `agent.json` and
replace its model placeholder. Set the configured key environment variable
privately, then follow the [CLI contract](cli.md). The normal safe tools are
available; shell and file overwrite remain disabled unless explicitly enabled.
Keep credentials, generated builds, workspace contents, and `.agent` transcripts
out of commits. To troubleshoot, check the command's stderr and JSONL error
events, configuration types and model ID, workspace access, and resource caps.
Do not upload unredacted transcripts or environment dumps.

## Local release archive

```bash
VERSION=v0.1.0 scripts/build-release.sh
```

This builds an archive without publishing. Publishing requires a version tag
and the [release workflow](../.github/workflows/release.yml). Archives contain
runtime executables, LICENSE, and NOTICE. See [contributing](../CONTRIBUTING.md)
for verification and review conventions.
