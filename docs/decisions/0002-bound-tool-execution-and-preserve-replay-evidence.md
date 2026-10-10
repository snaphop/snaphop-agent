# 2. Bound tool execution and preserve replay evidence

Date: 2026-10-10

## Status

Accepted

## Context

Tool-heavy turns need useful results within prompt limits, low repeated setup
cost, and faster independent reads. Interrupted turns must preserve completed
effects without guessing whether a missing result means an action never ran.
The runtime remains headless, framework-free, and bounded in memory and threads.

## Decision

Cache definitions when tools register, freeze the registry when the runtime
takes ownership, and share canonical schemas between local validation and both
provider adapters. Prepare validated, policy-approved calls before execution.

Opt-in read-only tools promise thread safety and no filesystem/process
mutations. A runtime-owned lazy worker pool runs batches containing only such
calls, with configurable concurrency and a fixed 32-job queue. Mixed or rejected
batches execute sequentially. The turn thread emits and persists results in
model order and joins submitted work before releasing turn-local state.

Bound serialized tool content, paginate UTF-8 reads using source-byte offsets,
and preserve diagnostic tails for command output. Under prompt pressure, trim
older results only in the model view while retaining native call/result pairs.
A bounded progress guard and independent call/iteration limits stop stalled
turns.

Give executions unique local IDs and retain separate provider IDs for native
wire correlation. Replay pairs completed results with their assistant batch and
supplies explicit unknown-outcome errors for missing results. Replay never
executes tools or rewrites persisted evidence.

## Consequences

Independent slow reads can overlap; cheap tools may incur scheduling overhead,
so concurrency can be set to one. Read-only execution is a tool-author contract
and provides no filesystem snapshot. File pagination assumes unchanged input.
Unknown mutation outcomes require inspecting state before retrying. Existing
transcripts without separate provider IDs remain supported, while callers that
send undeclared arguments now receive validation errors.
