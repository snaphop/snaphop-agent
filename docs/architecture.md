# Architecture

> A high-level map of the system. Keep it short. Detailed decisions live in
> [`decisions/`](./decisions/) as ADRs.

## Context

<!-- What problem does this system solve? Who uses it? What are the
non-functional requirements (latency, throughput, availability, compliance)? -->

## High-level design

<!-- A diagram or short prose describing major components and how data flows
between them. -->

```
[ client ] ──HTTP──> [ api ] ──> [ db ]
                       │
                       └─events──> [ worker ]
```

## Components

### `src/<component-a>/`

<!-- Responsibility, public interface, key dependencies. -->

### `src/<component-b>/`

<!-- ... -->

## Data model

<!-- Key entities and their relationships. Link to schema files. -->

## External dependencies

| Service | Purpose | Failure mode |
|---------|---------|--------------|
|         |         |              |

## Deployment

<!-- Where it runs, how it's deployed, how config and secrets are supplied. -->

## Observability

<!-- Logs, metrics, traces. What gets paged. -->
