#!/usr/bin/env bash
# Shared build concurrency. Source after enabling strict shell error handling.
build_jobs="${BUILD_JOBS:-${CMAKE_BUILD_PARALLEL_LEVEL:-$(getconf _NPROCESSORS_ONLN)}}"
if [[ ! "$build_jobs" =~ ^[1-9][0-9]*$ ]]; then
    echo "BUILD_JOBS/CMAKE_BUILD_PARALLEL_LEVEL must be a positive integer" >&2
    exit 1
fi
