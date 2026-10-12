## Contributing

Yggdrasilcore is a privately managed repository. We do not recommend emulation
enthusiasts follow it. If you want to contribute to emulation generally,
contribute to the upstream [AzerothCore project](https://www.azerothcore.org/wiki/contribute)
instead.

## What we accept here

- Cherry-picks and fixes that serve Yggdrasil WoW's Blizzlike gameplay.
- Blizzlike evidence is interpreted loosely: sniffs, Wowhead data, or video
  evidence are satisfactory. It does not have to be sniff-only.
- Stable, functional code is left alone. Do not refactor working systems for
  style or architecture reasons.

## What we do not accept

- General feature requests or content unrelated to Yggdrasil WoW.
- Refactors of stable code, format-only churn, or upstream sync requests.
- Changes that cannot build as described below.

## Supported build environments (only)

- Docker, as defined in `apps/docker/Dockerfile` and `docker-compose.yml`.
- Native Ubuntu 24.04, as defined in `.github/workflows/core-build.yml`.

We do not support any other OS, distro version, compiler, IDE setup
(VSCode/devcontainer), Nix flakes, the `acore.sh` installer flow, or repacks.
Bug reports and pull requests using those will be closed.

## Pull request requirements

- It builds: `docker compose build` and the Ubuntu 24.04 CI build pass.
- It is tested in-game by the author. Describe what you tested and what
  regressions you checked (spells, scripts, SAI, DB changes).
- Cherry-picks credit the original authors via the proper `--author` tag.
- Read the [project introduction](../.github/README.md) and the
  [Code of Conduct](./CODE_OF_CONDUCT.md) first.
