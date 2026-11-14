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
  build-only (milestone 17); Android goes through Vulkan (milestone 18,
  cross-compiled with NDK r27c, not executed on hardware), with Windows and
  Linux Vulkan being the extra capability; see the Platforms section.
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
| Linux x86_64 | Vulkan (extra capability) | Implemented, validated under lavapipe (milestones 18 and 19) |
| Windows x86_64 | Vulkan (extra capability) | Cross-compiled against the winevulkan loader surface, not executed (milestone 18) |
| Android (arm64-v8a) | Vulkan (primary target) | Cross-compiled with NDK r27c; the engine runs through the APK app shell (milestone 23, decision 52) |
| macOS, iOS | Metal | Compiles on any platform; mapping tests run on Linux; hardware run executed on the Apple A18 Pro (milestone 20) |

Linux is implemented and validated. Windows is cross-compiled with MinGW-w64
and validated under Wine (milestone 17): the tree builds clean under -Werror
and the full render pipeline (context, mesh, shader, lighting, shadows, post
chain, readback) executes under Wine. This is Wine, not a native Windows run.
Android is recorded here so the intended scope is explicit rather than
implied: the Vulkan backend (milestone 18) is the Android path, the tree
cross-compiles clean for Android with NDK r27c, and the Vulkan backend has
not been executed on Android hardware in the delivered increments.

Mobile platforms (Android and iOS) cannot run a bare executable: Android's
`SDL_Init` needs an `Activity` with a JNI environment and an `ANativeWindow`,
and iOS has no shell at all. The engine's application layer is a library
(`grace_app`), not a process, so the mobile build links the same `main.cpp`
behind a shared library (`libmain.so`) that carries the whole engine
statically plus the static SDL2 with its JNI entry points (`JNI_OnLoad`), and
the SDL2 `android-project` template loads it through `SDLActivity` (decision
52). The engine code is unchanged: the same sources build the Linux
executable, the Windows executable, the macOS executable and the Android
shared library. iOS is the same shape — an application shell that calls the
engine's `main` with Metal behind it — with the shell itself not a delivered
increment for iOS.

Windows is expected to be a small change rather than a port. The window and the
context come from SDL2, the render contract is API-neutral, and the OpenGL entry
points are resolved at runtime through `SDL_GL_GetProcAddress` instead of being
linked directly, so there is no import library to replace. What the cross-compile
changed: the CMakeLists gained the Windows branch (opengl32 and the Windows
system libraries for a static SDL2, the MSVC warning set, the MinGW static
runtime), the GL headers load glcorearb.h (MinGW's gl.h is GL 1.1 only) and
shield the `near`/`far` pseudo-modifiers, and the PhysX adapter is Linux-only
by build option.

Vulkan (milestone 18, behind `GRACE_ENABLE_VULKAN`) is the one backend for
Android, Windows and Linux, with Android as its primary target and the other
two as the extra capability on top of the implemented OpenGL backend. The
Android and Windows Vulkan rows are build-only validations: the engine has
not executed Vulkan on either.

Milestone 19 (decision 46) closes what the presentation engine on hardware
needs: the swap interval maps onto the present mode, the acquire retries on
the out-of-date resize, the depth attachment format is picked from the
device's format properties, and the sampler descriptors are written at draw
time from the bound texture views. The Vulkan backend is closed — the next
milestone has no Vulkan work.

Metal (milestone 20, behind `GRACE_ENABLE_METAL`) is the macOS and iOS
backend. Its loader declares its own types (the published enum values and
the published C layouts) and resolves the Objective-C runtime and the
Metal framework at runtime, so no Apple SDK is needed to build it and the
module compiles on any platform. Its MSL 2.0 shaders compile at runtime
through MTLLibrary, so there is no build-time shader step. The recorded
validation is the Linux compile, the pure mapping tests and the hardware
run on the Apple A18 Pro — the 10-frame forward path with violations 0 and
test_metal_render 31 checks, 0 failures (decision 47).

The core-completion milestone (milestone 21) adds the four core
capabilities the documented feature set names that the engine had not yet
implemented: the curve set (spline, B-spline and TCB), the task-based
multithreading (a pool of workers running a declared dependency graph,
with the deterministic-closure rule), the built-in profiler (zone timers
and the violation counters) and the compressed-texture support (BC1-BC5
decoded on the CPU into RGBA8). All four are platform-neutral and live in
the core library; the same code compiles on Linux, Windows and macOS (decision 48).

The audio platform backends (milestone 22, decision 49) close the audio
module's platform matrix: the XAudio2 backend (Windows) behind
`GRACE_ENABLE_XAUDIO2` and the OpenSL ES backend (Android) behind
`GRACE_ENABLE_OPENSL`, both implementing the same audio contract the null
backend and the OpenAL Soft adapter already implement. The XAudio2 backend
is validated under Wine against the real xaudio2_8.dll the Wine prefix
provides; the OpenSL ES backend is build-validated with the NDK (the suite
runs on Android hardware, not in this environment).

The 2D path milestone (milestone 23, decision 51) adds the standalone 2D
texture resource (create, fill, sample, read back) on the null, OpenGL,
Vulkan and Metal backends, the Sprite2D shader program (a textured quad in
NDC space with the tint colour and amount) on the shader libraries of the
three shader languages, and the procedural tween in the core library (the
Ease set, the scalar Tween and the two-component Tween2). The 2D texture
contract runs in the shared contract checks on every backend, and the new
test_tween suite proves the easing endpoints and the tween lifecycle.

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
The build fetches the SDL2 sources from the pinned submodule and builds
them as part of the configure step, so the SDKs are not system packages.
The `glslangValidator` tool is the one build-time prerequisite the
`GRACE_ENABLE_VULKAN` option cannot provide itself: the CMake configuration
fails with `find_program` if it is not on the `PATH` when that option is
enabled. Install it per platform before configuring with Vulkan.

### Linux

On Kubuntu/Debian the build needs the compiler, CMake, Ninja and the
GLVND OpenGL headers (linked when `GRACE_ENABLE_OPENGL=ON`):

```
sudo apt install build-essential cmake ninja-build \
  libglvnd-dev libgl1-mesa-dev
```

For the Vulkan backend, add the validator and the runtime loader:

```
sudo apt install glslang-tools
```

To execute the Vulkan tests on a machine without a Vulkan-capable GPU,
add the Mesa software driver (lavapipe) and the Vulkan utilities the
validation records use:

```
sudo apt install mesa-vulkan-drivers vulkan-tools
```

### Windows (cross-compiled from Linux)

The Windows build is a cross-compile from a Linux host with MinGW-w64
and is validated under Wine:

```
sudo apt install gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 wine
```

The same `glslangValidator` from the host (`glslang-tools`) is used;
the CMake `find_program` accepts it. The configure command is:

```
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
  -DGRACE_ENABLE_OPENGL=ON
```

### Android (cross-compiled with the NDK)

Download the NDK r27c and point the toolchain file at it:

```
cmake -S . -B build-android -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 \
  -DGRACE_ENABLE_VULKAN=ON
```

The Android build is validated as build-only (the binaries are not
executed on hardware in the delivered increments).

The OpenSL ES audio backend adds one option on the same configure:
`-DGRACE_ENABLE_OPENSL=ON`. The backend links the `OpenSLES` system
library the NDK sysroot provides — no download and no submodule; see the
Dependencies section.

The app shell (decision 52) builds the same engine as a shared library
behind SDLActivity: the tree's `android/` directory carries the SDL2
`android-project` template's overlay (the manifest, the `GraceActivity`
subclass and the strings) and `tools/build-android-apk.sh` assembles the
APK. The engine's application layer is a library, not a process, so the
engine code is unchanged — the same `main.cpp` builds the Linux, Windows
and macOS executables and this shared library:
```
cmake -S . -B build-android-apk -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 \
  -DGRACE_ENABLE_VULKAN=ON -DGRACE_ENABLE_ANDROID_SHELL=ON \
  -DGRACE_BUILD_TESTS=OFF
cmake --build build-android-apk --parallel
ANDROID_SDK=$SDK GRACE_MAIN_SO=build-android-apk/lib/libmain.so \
  sh tools/build-android-apk.sh
```
The script writes `build-android-apk/og-signed.apk` (com.opengrace.app);
the APK carries the whole engine statically plus the static SDL2 with its
JNI entry points (`JNI_OnLoad`), and the `GraceActivity` subclass passes
the fixed command line (`--vulkan --frames 10 -v`) through `getArguments()`.
The script needs the Android build-tools (aapt2, d8, zipalign, apksigner)
on the build machine; it is the shell's own packaging step, not a new
engine dependency.
### Default build
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```
The Vulkan backend's CLI runs on the same binary: `grace --vulkan` selects
the Vulkan backend when the build has `GRACE_ENABLE_VULKAN=ON` (with
`--hidden` and `--swap-interval` for the presentation engine's two knobs).
Build options:

| Option | Default | Meaning |
| --- | --- | --- |
| `GRACE_BUILD_TESTS` | `ON` | Build and register the deterministic tests with CTest. |
| `GRACE_WARNINGS_AS_ERRORS` | `ON` | Treat compiler warnings as errors. |
| `GRACE_ENABLE_OPENGL` | `OFF` | OpenGL backend (SDL2 submodule, OpenGL at runtime). |
| `GRACE_ENABLE_VULKAN` | `OFF` | Vulkan backend (SDL2 submodule, Vulkan at runtime, glslangValidator at build time). |
| `GRACE_ENABLE_PHYSX` | `OFF` | PhysX 3.4 physics backend (the submodule). |
| `GRACE_ENABLE_BULLET` | `OFF` | Bullet physics backend (the submodule). |
| `GRACE_ENABLE_BOX2D` | `OFF` | Box2D physics backend (the submodule). |
| `GRACE_ENABLE_ANGELSCRIPT` | `OFF` | AngelScript backend (the submodule). |
| `GRACE_ENABLE_OPENAL` | `OFF` | OpenAL Soft audio backend + Ogg/Vorbis. |
| `GRACE_ENABLE_XAUDIO2` | `OFF` | XAudio2 audio backend (Windows only). |
| `GRACE_ENABLE_OPENSL` | `OFF` | OpenSL ES audio backend (Android only). |
| `GRACE_ENABLE_ANDROID_SHELL` | `OFF` | Android app shell: the shared library (`libmain.so`) SDLActivity loads from the APK. |
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
| Vulkan loader and ICD | Apache-2.0 | Graphics API, resolved at runtime through SDL | linked when `GRACE_ENABLE_VULKAN=ON` |
| [glslangValidator](https://github.com/KhronosGroup/glslang) | Apache-2.0 / BSD-3 | Compiles the Vulkan shader library to SPIR-V at build time | build-time tool of `GRACE_ENABLE_VULKAN`, never linked |
| zlib | zlib | Deflate codec behind the OWD container | linked unconditionally |
| [NVIDIA PhysX SDK 3.4](https://github.com/NVIDIAGameWorks/PhysX-3.4) | 3-Clause BSD | Physics backend (extra physics: cloth, soft body) | linked when `GRACE_ENABLE_PHYSX=ON` |
| [Bullet Physics SDK](https://github.com/bulletphysics/bullet3) | zlib | Physics backend | linked when `GRACE_ENABLE_BULLET=ON` |
| [Box2D](https://github.com/erincatto/box2d) | MIT | 2D physics backend | linked when `GRACE_ENABLE_BOX2D=ON` |
| [AngelScript](https://github.com/anjo76/angelscript) | zlib | Text scripting host | linked when `GRACE_ENABLE_ANGELSCRIPT=ON` |
| [OpenAL Soft](https://github.com/kcat/openal-soft) | LGPL-2.0-or-later | Audio backend | linked when `GRACE_ENABLE_OPENAL=ON` |
| [libogg](https://github.com/xiph/ogg) / [libvorbis](https://github.com/xiph/vorbis) | BSD-style | Ogg Vorbis decode in the audio module's data layer | linked when `GRACE_ENABLE_OPENAL=ON` |
| [FreeType](https://freetype.org) | FTL (BSD-style) or GPL-2.0 | Font rasterisation in the GUI module | linked when `GRACE_ENABLE_FREETYPE=ON` |
| [libtheora](https://theora.org) | BSD-style (3-Clause) | Ogg Theora decode in the video module's data layer | linked when `GRACE_ENABLE_THEORA=ON` |
| OpenSL ES | Android system library | Audio backend on Android | linked when `GRACE_ENABLE_OPENSL=ON` (Android only) |
| XAudio2 | Windows system library | Audio backend on Windows | linked when `GRACE_ENABLE_XAUDIO2=ON` (Windows only) |

The last two rows are platform APIs, not fetchable dependencies, and are
deliberately absent from the submodule list. OpenSL ES comes from the NDK's
sysroot for the target API level (the build links the `OpenSLES` system
library directly) and XAudio2 comes from the Windows SDK on the target
platform; neither has a source repository to pin, so neither gets a
submodule entry, and their licence terms ride on the platform itself.

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
