#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

: "${VERSION:?Set VERSION to a vX.Y.Z release tag}"
if [[ ! "$VERSION" =~ ^v[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?$ ]]; then
    echo "VERSION must be vX.Y.Z or vX.Y.Z-prerelease" >&2
    exit 1
fi
if [[ "$(uname -s)" != Linux || "$(uname -m)" != x86_64 ]]; then
    echo "Release packaging currently supports Linux x86_64" >&2
    exit 1
fi

cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build-release --parallel "${BUILD_JOBS:-2}"
python3 scripts/check_protocol.py build-release

package="snaphop-agent-${VERSION}-linux-x86_64"
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/$package/bin" "$stage/$package/examples" dist
cp build-release/agent_cli build-release/agentd "$stage/$package/bin/"
cp LICENSE NOTICE README.md SECURITY.md "$stage/$package/"
cp examples/config.example.json examples/stdin_session.example.jsonl "$stage/$package/examples/"
"$stage/$package/bin/agent_cli" --help
"$stage/$package/bin/agentd" --help
tar -czf "dist/$package.tar.gz" -C "$stage" "$package"
cp LICENSE NOTICE dist/
(
    cd dist
    sha256sum "$package.tar.gz" LICENSE NOTICE > SHA256SUMS
    sha256sum --check SHA256SUMS
)
