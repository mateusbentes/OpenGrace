# OpenGrace

Independent, source-only reimplementation of the GRACE 2 engine architecture,
targeting native Linux x86_64 with OpenGL. Apache-2.0 licensed.

**This repository ships no game assets, no proprietary code, and no SDK
material.** It is an independent implementation built from publicly documented
behaviour and published technical descriptions of the original engine.

## What GRACE 2 was

GRACE 2 was the in-house engine of Reality Pump Studios, a division of TopWare
Interactive, based in Kraków, Poland. It succeeded the GRACE engine used by
*Two Worlds* (2007) and *Two Worlds II* (2010), and powered *Raven's Cry* (2015)
and the unreleased *Sacrilegium*. The studio ceased development in 2015 and no
source release ever occurred.

A public beta of the GRACE 2 SDK was distributed between 2012 and roughly 2014
under a free non-commercial licence. That channel no longer exists, and this
project does not use, include, or derive from any part of it.

This project is not affiliated with, endorsed by, or connected to Reality Pump
or TopWare Interactive.

## Goals

- Reimplement the documented GRACE 2 architecture as original source code.
- Keep every subsystem behind a platform-neutral contract.
- Target Linux x86_64 with OpenGL only; other platforms are out of scope.
- Validate each subsystem with deterministic tests and observable behaviour.
- Document design decisions, limitations, and validation results clearly.

## Scope and boundaries

This repository contains only original source code, tests, build files, and
documentation created for this project.

The following are intentionally excluded:

- Original game executables, libraries, and SDK files.
- Original game data, assets, scripts, maps, audio, video, and archives.
- Memory dumps, extracted resources, disassembler databases, and traces.
- Decompiled code, copied pseudocode, proprietary symbols, and internal tables.

## Documentation

- [docs/grace-architecture.md](docs/grace-architecture.md) — the architectural
  reference, compiled from public sources, that defines the fidelity baseline
  for this project.
- [docs/implementation-status.md](docs/implementation-status.md) — milestone
  progress, validation records and the open decisions.

## Building

Requirements: CMake 3.16 or newer and a C++17 compiler (GCC or Clang).

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Build options:

| Option | Default | Meaning |
| --- | --- | --- |
| `GRACE_BUILD_TESTS` | `ON` | Build and register the deterministic tests with CTest. |
| `GRACE_WARNINGS_AS_ERRORS` | `ON` | Treat compiler warnings as errors. |
| `GRACE_ENABLE_OPENGL` | `OFF` | Build the Linux OpenGL backend. Requires SDL2 and OpenGL. |
| `GRACE_ENABLE_PHYSX` | `OFF` | Build the PhysX 3.4 physics backend from a local SDK clone. |

## Dependencies

Nothing is vendored. Optional dependencies are cloned into local, git-ignored
directories and linked as external libraries. [NOTICE](NOTICE) holds the full
attribution entries and their licence terms.

| Dependency | Licence | Used for | Status |
| --- | --- | --- | --- |
| SDL2 | zlib | Window, context and input | linked when `GRACE_ENABLE_OPENGL=ON` |
| OpenGL and GLVND | MIT-style | Graphics API, resolved at runtime | linked when `GRACE_ENABLE_OPENGL=ON` |
| NVIDIA PhysX SDK 3.4 | 3-Clause BSD | Physics backend | planned, `GRACE_ENABLE_PHYSX` |
| Bullet Physics SDK | zlib | Physics backend | planned |
| Box2D | MIT | 2D physics backend | planned |
| AngelScript | zlib | Text scripting host | planned |
| OpenAL Soft | LGPL-2.0-or-later | Audio backend, linked dynamically | planned |
| FreeType | FreeType License | Font rendering in the UI module | planned |

## Repository layout

```
include/grace/     Public C++ interfaces, one directory per engine module
src/               Implementations mirroring include/
tests/             Deterministic tests (no external framework)
docs/              Architectural reference and implementation status
third_party/       Licence texts and attribution notices for dependencies
```

## Development principles

1. Implement one narrow subsystem at a time.
2. Prefer behaviourally specified interfaces over assumptions about internals.
3. Keep uncertain behaviour isolated behind explicit experimental contracts.
4. Add tests before relying on real content.
5. Preserve fallbacks so the project remains buildable without external data.
6. Review all changes manually before committing or publishing them.

## Contributions

Contributions are welcome when they follow the project boundaries above.
Proposed changes should include focused documentation and tests where practical.
Do not submit proprietary files, extracted content, decompiled output, or code
derived from the original engine.

## Licence

The original source code in this repository is licensed under the Apache
License 2.0. See [LICENSE](LICENSE) for the full licence text and
[NOTICE](NOTICE) for third-party attribution.
