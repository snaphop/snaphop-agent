#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/build-common.sh

build_dir="${1:-build-coverage}"
python="${PYTHON:-python3}"
"$python" -m gcovr --version
cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DAGENT_ENABLE_COVERAGE=ON
cmake --build "$build_dir" --parallel "$build_jobs"
# Reset counters so reports describe this run, rather than accumulated history.
find "$build_dir" -name '*.gcda' -type f -delete
ctest --test-dir "$build_dir" --output-on-failure
"$python" scripts/check_protocol.py "$build_dir"
mkdir -p "$build_dir/coverage"
"$python" -m gcovr --root . --filter '^(src|include/agent)/' \
    --gcov-executable "${GCOV:-gcov}" -j "$build_jobs" \
    --exclude-unreachable-branches --exclude-throw-branches \
    --html-details "$build_dir/coverage/index.html" \
    --xml "$build_dir/coverage/cobertura.xml" \
    --json-summary "$build_dir/coverage/summary.json" --print-summary \
    --fail-under-line "${COVERAGE_MIN_LINES:-92}" \
    --fail-under-branch "${COVERAGE_MIN_BRANCHES:-80}" "$build_dir"
