# GitHub migration — 2026-10-10

The canonical repository is now
[snaphop/snaphop-agent](https://github.com/snaphop/snaphop-agent).
Its open source setup follows
[snaphop/snaphop-agent-vm](https://github.com/snaphop/snaphop-agent-vm): MIT
licensing, contributor and private security-reporting documentation, a PR
template, grouped weekly Actions updates, verification on PRs and `master`,
and releases only on version tags. Build and packaging are adapted for C++23,
CMake, and dynamically linked libcurl rather than Go.

All source commits through Gitea `master` at
`6f6409d0ecdc9f75746d3fe68068d9bb5684061c` are preserved with their original
identities and merge commits. The source had one branch, no tags, no releases,
and no standalone issues at migration time. Existing merged PR history stays
in Git and the original discussions remain linked below; they are not recreated
as new GitHub pull requests.

| Gitea PR | Change |
|---------|--------|
| [#1](https://git.snaphop.xyz/snaphop/snaphop-agent/pulls/1) | Refresh and harden the headless v0 agent |
| [#2](https://git.snaphop.xyz/snaphop/snaphop-agent/pulls/2) | Remove the previous Actions workflow |
| [#3](https://git.snaphop.xyz/snaphop/snaphop-agent/pulls/3) | Optimize tool calling, bounded execution, and interrupted replay |

The new GitHub workflows supersede the earlier Gitea workflow removal as part
of matching the public reference project. No initial release tag is created by
the migration. Runtime acceptance gaps remain in [status.md](./status.md).

Existing clones can keep the old remote and switch to GitHub:

```bash
git remote rename origin gitea
git remote add origin git@github.com:snaphop/snaphop-agent.git
git fetch origin
git branch --set-upstream-to=origin/master master
```
