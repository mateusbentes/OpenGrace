# OpenGrace

Independent, source-only reimplementation of the GRACE 2 engine architecture,
targeting native Linux x86_64 with OpenGL. Apache-2.0 licensed.

The engine is implemented on Linux x86_64 with OpenGL, and is cross-compiled
for Windows x86_64 (MinGW-w64), validated under Wine.

**This repository ships no game assets, no proprietary code, and no SDK
material.** It is an independent implementation built from publicly documented
behaviour and published technical descriptions of the original engine.

## What GRACE 2 was

GRACE 2 was the in-house engine of Reality Pump Studios, a division of TopWare
Interactive, based in Kraków, Poland. It succeeded the original GRACE engine,
the one *Two Worlds II* (2010) ran on; the official site for that game credits
"the all-new GRACE game engine". The engine behind the first *Two Worlds*
(2007) is not publicly documented, and this project does not claim it was a
GRACE version. The official
site displayed screenshots from games created on GRACE 2, among them the
unreleased *Sacrilegium*, and the architectural reference records what that
evidence does and does not support. The studio ceased development in 2015 and
no source release ever occurred.

A public beta of the GRACE 2 SDK was distributed between 2012 and roughly 2014
under a free non-commercial licence. That channel no longer exists, and this
project does not use, include, or derive from any part of it.

This project is not affiliated with, endorsed by, or connected to Reality Pump
or TopWare Interactive.

## Goals

- Reimplement the documented GRACE 2 architecture as original source code.
- Provide the resource container the documented feature set implies, named OWD for Open World Data, designed
  from the requirements a runtime has rather than copied from any product; see
  the resource container specification.
- Keep every subsystem behind a platform-neutral contract.
- Target Linux x86_64 with OpenGL as the implemented platform. Windows is
  cross-compiled and validated under Wine (milestone 17) and Android remains
  planned through Vulkan, with Windows and Linux Vulkan being the extra
  capability; see the Platforms section.
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

## Running it

`grace` with no arguments renders through the HDR post-processing chain
(HDR target, bloom, filmic tonemap + sRGB display encode, SSAO, morphological
AA; depth of field is off by default because its focus distance is scene
content) on the forward path and prints the centre pixel of the frame:
`center_pixel=110,110,110,255` for the default clear colour 30 — derivable as
round(255 * sRGB(aces(30/255))) = 110. The pre-chain legacy linear->8-bit
output is still available as a compatibility path: `grace --no-post` prints
`center_pixel=30,30,30,255`.
The `owdtool` command line packs a directory into an OWD container, unpacks
one and lists one:
```
owdtool pack <directory> <container.owd>
owdtool unpack <container.owd> <directory>
owdtool list <container.owd>
```
The container format is the OWD specification
([docs/resource-container.md](docs/resource-container.md)), and the tool is
the writer and the reader behind it driven by hand. The scene module
serialises its state into the container as one entry, so the same data can
be read back by the overlay loader.

## Documentation

- [docs/grace-architecture.md](docs/grace-architecture.md) — the architectural
  reference, compiled from public sources, that records what the vendor
  claimed about the engine and what this project reconstructs against.
- [docs/implementation-status.md](docs/implementation-status.md) — milestone
  progress, validation records and the open decisions.

## Platforms

| Platform | Graphics API | Status |
| --- | --- | --- |
| Linux x86_64 | OpenGL 3.3 core, falling back to 3.0 | Implemented and validated |
| Windows x86_64 | OpenGL | Cross-compiled, validated under Wine (milestone 17) |
| Android | Vulkan | Planned, not started |

Linux is implemented and validated. Windows is cross-compiled with MinGW-w64
and validated under Wine (milestone 17): the tree builds clean under -Werror
and the full render pipeline (context, mesh, shader, lighting, shadows, post
chain, readback) executes under Wine. This is Wine, not a native Windows run.
Android is recorded here so the intended scope is explicit rather than
implied, and nothing in this repository currently supports it; no build file,
shader or source file has been adapted for it.

Windows is expected to be a small change rather than a port. The window and the
context come from SDL2, the render contract is API-neutral, and the OpenGL entry
points are resolved at runtime through `SDL_GL_GetProcAddress` instead of being
linked directly, so there is no import library to replace. What the cross-compile
changed: the CMakeLists gained the Windows branch (opengl32 and the Windows
system libraries for a static SDL2, the MSVC warning set, the MinGW static
runtime), the GL headers load glcorearb.h (MinGW's gl.h is GL 1.1 only) and
shield the `near`/`far` pseudo-modifiers, and the PhysX adapter is Linux-only
by build option.

Android is planned through Vulkan, not OpenGL ES. The OpenGL ES profile is a
different API profile with a different shading language: the shaders target
GLSL 3.30 core and would need a GLSL ES 3.00 path, and that difference would
have to be absorbed inside the OpenGL backend so the render contract stays
API-neutral. The application lifecycle also differs, because the application
does not own its window and the context can be lost and recreated. None of
that is implemented.

The scripting backends are not wired yet, so neither platform introduces a
dependency that is currently unavailable there. The physics backends are
behind build options and the PhysX adapter is Linux-only: the adapter links
the SDK's linux64 Makefiles build, and the CMake configuration stops with
`FATAL_ERROR` when GRACE_ENABLE_PHYSX is on and the target is not Linux. The
SDK upstream lists Windows, Linux, OSX, Android and iOS among its supported
platforms, so a Windows PhysX backend would come from the SDK's own Windows
build, not from milestone 17.

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
| `GRACE_ENABLE_OPENGL` | `OFF` | OpenGL backend (SDL2 submodule, OpenGL at runtime). |
| `GRACE_ENABLE_PHYSX` | `OFF` | PhysX 3.4 physics backend (the submodule). |
| `GRACE_ENABLE_BULLET` | `OFF` | Bullet physics backend (the submodule). |
| `GRACE_ENABLE_BOX2D` | `OFF` | Box2D physics backend (the submodule). |
| `GRACE_ENABLE_ANGELSCRIPT` | `OFF` | AngelScript backend (the submodule). |
| `GRACE_ENABLE_OPENAL` | `OFF` | OpenAL Soft audio backend + Ogg/Vorbis. |
| `GRACE_ENABLE_FREETYPE` | `OFF` | FreeType GUI font adapter (the submodule). |
| `GRACE_ENABLE_THEORA` | `OFF` | Theora video decode adapter + Ogg/Vorbis. |

## Dependencies

No dependency is vendored. The middleware SDKs are pinned as git submodules at
the revisions the build is tested against; each one is built on demand by the
project's CMake configuration, outside the engine's warning set, and installed
into the build tree (the PhysX SDK keeps its own Makefile build and links the
static libraries it produces).

| Dependency | Licence | Used for | Status |
| --- | --- | --- | --- |
| SDL2 | zlib | Window, context and input | linked when `GRACE_ENABLE_OPENGL=ON` |
| OpenGL and GLVND | MIT-style | Graphics API, resolved at runtime | linked when `GRACE_ENABLE_OPENGL=ON` |
| zlib | zlib | Deflate codec behind the OWD container | linked unconditionally |
| [NVIDIA PhysX SDK 3.4](https://github.com/NVIDIAGameWorks/PhysX-3.4) | 3-Clause BSD | Physics backend (extra physics: cloth, soft body) | linked when `GRACE_ENABLE_PHYSX=ON` |
| [Bullet Physics SDK](https://github.com/bulletphysics/bullet3) | zlib | Physics backend | linked when `GRACE_ENABLE_BULLET=ON` |
| [Box2D](https://github.com/erincatto/box2d) | MIT | 2D physics backend | linked when `GRACE_ENABLE_BOX2D=ON` |
| [AngelScript](https://github.com/anjo76/angelscript) | zlib | Text scripting host | linked when `GRACE_ENABLE_ANGELSCRIPT=ON` |
| [OpenAL Soft](https://github.com/kcat/openal-soft) | LGPL-2.0-or-later | Audio backend | linked when `GRACE_ENABLE_OPENAL=ON` |
| [libogg](https://github.com/xiph/ogg) / [libvorbis](https://github.com/xiph/vorbis) | BSD-style | Ogg Vorbis decode in the audio module's data layer | linked when `GRACE_ENABLE_OPENAL=ON` |
| [FreeType](https://freetype.org) | FTL (BSD-style) or GPL-2.0 | Font rasterisation in the GUI module | linked when `GRACE_ENABLE_FREETYPE=ON` |
| [libtheora](https://theora.org) | BSD-style (3-Clause) | Ogg Theora decode in the video module's data layer | linked when `GRACE_ENABLE_THEORA=ON` |

The submodules are fetched once with:

```
git submodule update --init
```

[NOTICE](NOTICE) holds the full attribution entries and their licence terms.

## Repository layout

```
include/grace/     Public C++ interfaces, one directory per engine module
src/               Implementations mirroring include/
tests/             Deterministic tests (no external framework)
docs/              Architectural reference and implementation status
third_party/       Licence texts and attribution notices for dependencies
tools/             Development and verification scripts
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
