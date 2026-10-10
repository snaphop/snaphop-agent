#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

build_dir="${1:-build}"
cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build "$build_dir" --parallel "${BUILD_JOBS:-2}"
ctest --test-dir "$build_dir" --output-on-failure
python3 scripts/check_protocol.py "$build_dir"
