# Security Policy

## Supported

Only the `master` branch of Yggdrasilcore is supported, and only in these
build environments:

| Environment      | Status        |
| ---------------- | ------------- |
| Docker (as per `apps/docker/Dockerfile` and `docker-compose.yml`) | Supported |
| Native Ubuntu 24.04 (as per `.github/workflows/core-build.yml`)   | Supported |

## Not supported

- Any other OS (Windows, macOS), distro, or distro version.
- Non-Docker local builds, the `acore.sh` installer flow, Nix flakes,
  devcontainers, and IDE-specific setups.
- Any fork, repack, or precompiled binary.
- The abandoned upstream scripts and configs removed from this repository.

Reports against unsupported setups will be closed.

## Reporting a vulnerability

A vulnerability is any hack or exploit that harms server performance,
stability, or fairness (e.g. fly hacking, packet injection, crash or lag
exploits).

Report it by opening a GitHub issue in this repository with enough detail to
reproduce: what happened, where, and the simplest reproduction steps. Do not
include account credentials or private player data.
