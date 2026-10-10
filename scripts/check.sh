#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/build-common.sh
python3 scripts/test_build_scripts.py
python3 scripts/test_security_scripts.py

build_dir="${1:-build}"
cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build "$build_dir" --parallel "$build_jobs"
ctest --test-dir "$build_dir" --output-on-failure
python3 scripts/check_protocol.py "$build_dir"
scripts/static-analysis.sh "$build_dir"
scripts/security-check.sh "$build_dir"
