---
name: review-enhancements
description: Review and classify the repository's entire open enhancement, feature, and proposal queue without implementing it. Use for whole-queue enhancement triage; do not use for a single feature unless explicitly invoked.
---

# Review Enhancements

## Establish scope and authority

Read [AGENTS.md](../../../AGENTS.md), [SECURITY.md](../../../SECURITY.md),
[CONTRIBUTING.md](../../../CONTRIBUTING.md), [CODE_REVIEW.md](../../../CODE_REVIEW.md),
and the relevant [documentation](../../../docs/README.md).
Use the repository remote to select the issue host; the canonical repository is
`snaphop/snaphop-agent` and its default branch is `master`.
Honor actions already authorized in the session. Reviewing does not itself
authorize tracker comments or closures, commits, pushes, merges, branch deletion,
history rewrites, releases, or paid provider calls. Do authorized work without
asking again. Keep suspected vulnerabilities private under `SECURITY.md`.
Preserve pre-existing user changes and remove only temporary artifacts you created.

## Inventory and understand

Inventory all open enhancement requests using live issue types and labels,
including Feature and enhancement where present. Record the initial set and
recheck before finishing. Read the full issue, discussion, user need, proposed
solution, linked PRs, relevant history, architecture, and ADRs.
Separate the requested outcome from its proposed implementation. Check current
and released behavior before marking a request already supported.

## Classify against project ownership

The runtime owns turn lifecycle, sessions, transcript recovery and compaction,
prompt bounds, provider adaptation, typed tool execution and policy, event
emission, and file-backed memory. Operators own provider credentials, workspace
selection, configuration, and explicit dangerous/overwrite flags. Model output
is untrusted input; it cannot grant itself authority or change tool policy.

Apply the v0 non-goals in `AGENTS.md`: messaging gateways, dashboards, plugin
marketplaces, vector memory, remote administration, distributed execution, and
multi-agent routing require an explicit scope change. A VM supervisor's image,
hypervisor, networking, or guest provisioning concerns belong to that project.

Classify each request as valid, invalid, or needs information, with source-backed
reasoning. For valid requests, describe acceptance criteria, compatibility,
resource/security concerns, and affected ownership boundaries only as supported
by evidence. For invalid requests, explain the existing capability or scope
conflict. Ask targeted questions for missing information without requesting
secrets. Avoid speculative architecture and promises of delivery.

## Reconcile without implementing

Do not implement enhancements as part of triage. Post comments, change labels,
or close invalid requests only when already authorized, using existing host
taxonomy. Report every issue's ID, classification, evidence, open questions,
recommended next step, and final tracker state. Distinguish reviewed, accepted,
implemented, merged, and released states.
