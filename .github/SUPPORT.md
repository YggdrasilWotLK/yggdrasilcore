# Support

Yggdrasilcore is a privately managed repository. There is no public chat,
forum, or Discord for this project, and no general helpdesk.

## What we support

- Building and running the `master` branch via Docker (`docker compose build`,
  `docker compose up`) or natively on Ubuntu 24.04 (see
  `.github/workflows/core-build.yml`).
- Installation instructions are the same as upstream:
  [AzerothCore installation wiki](http://www.azerothcore.org/wiki/installation).

## What we do not support

- Any other OS, distro version, compiler, or build method.
- Repacks, forks, precompiled binaries, or third-party modules, except the
  modules adapted in the [YggdrasilWotLK repositories](https://github.com/YggdrasilWotLK?tab=repositories).
- General emulation questions. Those belong to the upstream
  [AzerothCore project](https://www.azerothcore.org/wiki/contribute), which
  enthusiasts should follow instead of this repository.

## Bug reports

Found broken Blizzlike behavior (spell, quest, creature, SAI, DB)?

1. Search the [existing issues](https://github.com/YggdrasilWotLK/yggdrasilcore/issues)
   first. If it is already reported, confirm it with a comment.
2. Otherwise open a new issue with: what broke, where (zone, NPC/item/spell
   IDs), step-by-step reproduction, and what regression testing you did.

Reports against unsupported setups will be closed. See
[CONTRIBUTING](./CONTRIBUTING.md) before opening pull requests and
[SECURITY](./SECURITY.md) for exploits.
