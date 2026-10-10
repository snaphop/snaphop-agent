#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/build-common.sh

build_dir="${1:-build}"
cppcheck="${CPPCHECK:-cppcheck}"
if ! command -v "$cppcheck" >/dev/null 2>&1; then
    echo "Static analysis requires Cppcheck 2.13+ (install cppcheck or set CPPCHECK)." >&2
    exit 1
fi
"$cppcheck" --version
# Standalone analysis also works before compilation. CMake fetches the headers
# and records the exact includes/defines for both production and test sources.
cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
report_dir="$build_dir/static-analysis"
mkdir -p "$report_dir/cache"

# Catch2 uses attribute feature detection and the _catch_sr literal suffix.
# These analyzer-only definitions let Cppcheck parse assertions without changing
# compiled tests or disabling their expressions. Dependency diagnostics alone
# are suppressed; project diagnostics (including parsing errors) fail the gate.
"$cppcheck" --project="$build_dir/compile_commands.json" \
    --file-filter='src/*' --file-filter='tests/*' \
    --file-filter="$PWD/src/*" --file-filter="$PWD/tests/*" \
    -i _deps \
    --std=c++23 --enable=warning,performance,portability --error-exitcode=1 \
    --library=posix --library=libcurl --inline-suppr \
    --suppress='*:*/_deps/*' \
    -D'__has_cpp_attribute(x)=0' -D'_catch_sr=' \
    --cppcheck-build-dir="$report_dir/cache" -j "$build_jobs" \
    2>&1 | tee "$report_dir/cppcheck.log"
