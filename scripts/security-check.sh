#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
umask 077
build_dir="${1:-build}"
trivy="${TRIVY:-trivy}"
if [[ -z "${TRIVY:-}" ]] && ! command -v "$trivy" >/dev/null 2>&1; then
    trivy=.tools/trivy/trivy
fi
if ! command -v "$trivy" >/dev/null 2>&1; then
    echo "Security checks require Trivy 0.75.0; run scripts/install-trivy.sh or set TRIVY." >&2
    exit 1
fi
"$trivy" --version
report_dir="$build_dir/security"
mkdir -p "$report_dir"
chmod 700 "$report_dir"
# Discard prior reports so a failed scan cannot leave stale evidence of success.
rm -f "$report_dir/repository.json" "$report_dir/dependencies.json" \
    "$report_dir/runtime.json" "$report_dir/licenses.json"
"${SECURITY_PYTHON:-python3}" scripts/security_inventory.py "$build_dir" "$report_dir"
common=(--cache-dir "$report_dir/cache" --timeout 10m --format json --exit-code 1 \
    --severity UNKNOWN,LOW,MEDIUM,HIGH,CRITICAL --ignorefile /dev/null \
    --skip-version-check)
# Exclude generated outputs and operator state, including custom build directories.
# Authored source, tests, scripts, documentation, .github and agent skills are scanned.
skip=(--skip-dirs .git --skip-dirs '.tools' --skip-dirs build --skip-dirs 'build-*' \
    --skip-dirs '.venv*' --skip-dirs .agent --skip-dirs workspace --skip-dirs dist \
    --skip-dirs "$build_dir" --skip-files agent.json)
status=0
scan() {
    local report="$1"
    shift
    if "$trivy" "$@" --output "$report_dir/$report.json" \
        > "$report_dir/$report.log" 2>&1; then
        echo "Trivy $report scan passed."
    else
        echo "Trivy $report scan failed; inspect private reports under $report_dir." >&2
        status=1
    fi
}
scan repository fs "${common[@]}" --scanners vuln,misconfig,secret \
    --secret-config '' --file-patterns 'pip:requirements-.*\.txt' "${skip[@]}" .
scan dependencies sbom "${common[@]}" --scanners vuln "$report_dir/dependencies.cdx.json"
scan runtime rootfs "${common[@]}" --scanners vuln "$report_dir/runtime-rootfs"
# Permissive licenses are inventory entries, not security failures. Retain the
# complete license report for review without imposing a new licensing policy.
scan licenses fs "${common[@]}" --exit-code 0 --scanners license --license-full "${skip[@]}" .
if ! "${SECURITY_PYTHON:-python3}" scripts/security_inventory.py --validate "$report_dir"; then
    status=1
fi
exit "$status"
