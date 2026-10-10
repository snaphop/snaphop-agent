# Tests and fixtures

Run `scripts/check.sh` from the repository root for the required local build,
script tests, C++ tests, loopback protocol checks, static analysis, and Trivy
security scans. See
[local setup](../docs/host-setup.md) for prerequisites and coverage commands.
C++ tests use Catch2 and are registered with CTest. Run a focused test by name
using `build/agent_tests 'test name'`, or inspect names with
`build/agent_tests --list-tests`.

## Evidence and fixture ownership

Runtime tests inject mock `IModelClient` responses. Filesystem/transcript tests
create scratch paths; shell tests execute local POSIX commands with bounded
outputs and deadlines. These tests do not require a model-provider account.
Keep credentials and user workspaces out of fixtures, and clean up test-created
state without touching pre-existing paths.

[scripts/check_protocol.py](../scripts/check_protocol.py) starts a loopback HTTP
server, supplies synthetic OpenAI/Anthropic JSON responses, and launches the
actual executables. It checks native tool continuation, schema stability,
transcript replay, CLI output, and malformed daemon input. Its key and model ID
are fake values; it does not establish that a real provider accepts a particular
account, model, endpoint, or response variant.

When adding provider or transcript fixtures, identify their source and the
behavior asserted. Distinguish synthetic responses from redacted real captures.
For a real capture, record adapter/API version, capture date, reproduction, and
redactions; never commit authorization headers, private prompts, or tool output
from user workspaces. Every stored fixture should be exercised by a test.
Cover failure/empty/partial results as well as successful ones. Keep native
call/result pairing intact unless the test deliberately targets invalid input.

Tests may share process environment or event-stream resources, so the canonical
suite runs sequentially. Concurrent compilation and bounded runtime worker
behavior are separate concerns. Performance tests tagged `[performance]` use
mock turns; their measurements exclude live-provider latency and are
observations rather than hard cross-machine gates.
