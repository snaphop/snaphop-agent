#!/usr/bin/env bash
# Install a verified, pinned scanner; never pipe downloaded code into a shell.
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ "$(uname -s)" != Linux || "$(uname -m)" != x86_64 ]]; then
    echo "The pinned installer supports Linux x86_64; install Trivy 0.75.0 for your platform." >&2
    exit 1
fi
install_dir="${1:-.tools/trivy}"
archive_dir="$(mktemp -d)"
trap 'rm -rf "$archive_dir"' EXIT
curl --fail --silent --show-error --location --retry 3 \
    https://github.com/aquasecurity/trivy/releases/download/v0.75.0/trivy_0.75.0_Linux-64bit.tar.gz \
    --output "$archive_dir/trivy.tar.gz"
printf '%s  %s\n' c6e65abddb348e25f10549df887045629cf28cc72453cd1c63acb717316b3f3f \
    "$archive_dir/trivy.tar.gz" | sha256sum --check --status
mkdir -p "$install_dir"
tar -xzf "$archive_dir/trivy.tar.gz" -C "$install_dir" trivy LICENSE
"$install_dir/trivy" --version
