# OpenGrace — Implementation Status

Running record of milestone progress, validation results and open decisions.
The architectural baseline is [grace-architecture.md](grace-architecture.md).

The `.docx` files in this directory are generated from the `.md` sources
with `pandoc` and are committed for convenience. Edit the Markdown, then
regenerate: `pandoc docs/implementation-status.md -o docs/implementation-status.docx`.

---

## Milestones

| # | Scope | Status |
| --- | --- | --- |
| 0 | Build system, module skeleton, version accessor | Done |
| 1 | Fixed-step clock, application lifecycle, null render backend | Done |
| 2 | OpenGL backend, function loader, diagnostic triangle | Done |
| 3 | Render foundation: math, mesh, shader, material | Done |
| 4 | Forward lighting: point, spot, directional, shadow maps | Planned |
| 5 | Deferred lighting: G-buffer, unlimited lights, light volumes | Planned |
| 6 | Post-processing: HDR, bloom, filmic tonemap, DOF, AA, SSAO-STF | Planned |
| 7 | Global illumination by light volume propagation | Planned |
| 8 | Physics contract and the first backend | Planned |
| 9 | Script host: AngelScript integration and type registration | Planned |
| 10 | Visual scripting: block registry and graph evaluation | Planned |
| 11 | Animation: skeletal, state machines, blending, events | Planned |
| 12 | Scene management: entities, spatial partitioning, queries, LOD | Planned |
| 13 | Audio: 2D/3D playback, streaming, reverb | Planned |
| 14 | UI: controls, FreeType fonts, unicode input | Planned |
| 15 | AI and gameplay services | Planned |

---

## Module status

| Module | State | Notes |
| --- | --- | --- |
| core | Partial | Version, fixed-step clock, header-only math (vec/mat4/transform/camera). Dependency-free, enforced by grep in review. |
| app | Partial | Application lifecycle driver and the CLI entry point. |
| render | Partial | Contract, null backend, OpenGL backend, loader, API-neutral mesh/shader/material plus their OpenGL implementations; diagnostic triangle rebuilt on the foundation. |
| physics | Not started | Contract designed, no backend linked. |
| script | Not started | AngelScript and the visual block system are in scope. |
| audio, video, gui, motion, ai, game | Not started | |

---

## Validation records

All results below are real runs, on the tree at the stated commit.

| Date | Environment | Result |
| --- | --- | --- |
| 2026-11-05 | Ubuntu 26.04.1, x86_64, GCC 15.2.0, CMake 4.2.3 | `7cb07b4` (skeleton): Debug 5/5, Release 5/5, zero warnings under `-Werror`; Clang 21.1.8 cross-check 5/5 |
| 2026-11-05 | same, plus SDL2 2.32.10, GLVND 1.7.0, Mesa 26.0.8, Xvfb 21.1.22 | `f3d47a7` (milestone 2): OpenGL off 6/6; OpenGL on 8/8 Debug and Release; `-L opengl` 2/2; zero warnings under `-Werror` |
| 2026-11-05 | same | Check counts: version 4, fixed_step_clock 42, application 26, opengl_context 17, opengl_render 32 — all zero failures |
| 2026-11-05 | same | Visible CLI run: `opengl 4.6 core`, vendor Intel, renderer Iris Xe, centre pixel `30,30,30,255`, depth 1.0, stencil 0, violations 0, gl_errors 0, exit 0 |
| 2026-11-05 | same, under `xvfb-run` | Hidden run: `llvmpipe 4.5 core`, same centre pixel, exit 0 |
| 2026-11-05 | same, `SDL_VIDEODRIVER=dummy` | No context possible: exit 1 with an explicit message; OpenGL tests would report 77 and be shown as Skipped |
| 2026-11-05 | same | Two identical headless runs: byte-identical output, both exit 0 |
| 2026-11-05 | same, milestone 3 | OpenGL off: 7/7 Debug; OpenGL on: 11/11 Debug and Release; `-L opengl` 4/4; zero warnings under `-Werror` |
| 2026-11-05 | same, milestone 3 | Check counts: version 4, fixed_step_clock 42, application 26, math 28, opengl_context 17, opengl_render 32, mesh 13, shader 16 — all zero failures |
| 2026-11-05 | same, milestone 3 | Visible CLI run: `opengl 4.6 core` Iris Xe, centre pixel `30,30,30,255`, exit 0; hidden run under `xvfb-run`: llvmpipe `4.5 core`, same pixel, exit 0. Broken-shader failures surface the real info log, e.g. ``error: `broken_identifier_xyz' undeclared`` |
| 2026-11-05 | same, contract extension | OpenGL off: 7/7 Debug; OpenGL on: 11/11 Debug and Release; `-L opengl` 4/4; zero warnings under `-Werror`; check counts: opengl_context 20, opengl_render 47 |

### Defects found by the tests, and fixed

| Defect | Cause | Resolution |
| --- | --- | --- |
| `glClear` raised `GL_INVALID_VALUE` and cleared nothing | Contract clear-buffer flags are `1`, `2`, `4`; GL expects `0x4000`, `0x100`, `0x400` | Explicit mapping to the GL mask |
| Off-centre readback returned undefined data | Readback happened after the buffer swap | Pre-swap `queueReadback()` and `lastReadback()` |
| OpenGL readback returned alpha 255 while the null backend returned the clear colour's alpha | The context was created without an alpha channel | Request `SDL_GL_ALPHA_SIZE` 8 |
| `describe()` reported "not running" after shutdown | Runtime state reported instead of historical fact | Version, vendor and renderer reported whenever a context was obtained |
| Mesh/shader tests drew with a uniform set before the program was active (`GL_INVALID_OPERATION`) | `glUniform*` requires a current program | Documented precondition in `shader.hpp` (setUniform requires `use()`); tests activate the program first |

The first defect is worth noting as a design signal: the contract uses its own
enumeration rather than GL constants, which is what keeps the null backend
implementable. The cost is an explicit mapping, and the `glGetError` counter
added in milestone 1 is what caught it.

---

## Dependency versions

Validated against: CMake 4.2.3, GCC 15.2.0, Clang 21.1.8, SDL2 2.32.10,
GLVND 1.7.0, Mesa 26.0.8 (Iris Xe and llvmpipe), Xvfb 21.1.22, on
Ubuntu 26.04.1 x86_64.

Build options:

| Option | Default | Meaning |
| --- | --- | --- |
| `GRACE_BUILD_TESTS` | `ON` | Build and register the deterministic tests. |
| `GRACE_WARNINGS_AS_ERRORS` | `ON` | Treat warnings as errors. |
| `GRACE_ENABLE_OPENGL` | `OFF` | Build the Linux OpenGL backend. |
| `GRACE_ENABLE_PHYSX` | `OFF` | Reserved; no backend is linked yet. |

---

## Target and deviations

The documented GRACE 2 target was Direct3D 9/10/11 and **OpenGL 3.0+**. This
project implements the OpenGL path only, as a deliberate subset. The context
request is 3.3 core with a fallback to 3.0 without a profile flag; profiles do
not exist before GL 3.2, so "3.0 core" is not a valid request.

### Deliberate deviations from the original

| Original | This project | Reason |
| --- | --- | --- |
| Direct3D 9/10/11 | Not implemented | Linux-only target; out of scope by decision |
| Console and mobile platforms | Not implemented | Out of scope by decision |
| "Fast SSE math library" | Scalar math, compiler autovectorisation | Hand-written intrinsics usually lose to the autovectoriser today and complicate the code substantially. Revisit only if profiling shows a need. |
| Network-based editors and tools | Deferred | Not required for engine milestones |
| Stereoscopic 3D | Deferred | No target hardware path in scope |

---

## Open decisions

1. **Forward or deferred rendering as the primary path?** The original supported
   both: forward with up to four dynamic lights per object plus sunlight, and
   deferred with unlimited lights. This must be settled before milestone 4,
   because it determines material layout and the lighting pass structure.
2. **Readback inside the contract or OpenGL-only?** It currently lives in the
   OpenGL backend, so the shared contract checks cannot verify framebuffer
   contents through the null backend. Promoting it to an optional capability
   would let both backends be verified identically.
3. **Fidelity of the visual scripting block set.** The original block vocabulary
   is not publicly documented. A functionally equivalent set may be acceptable,
   but the decision affects the AngelScript type registration interface.
4. **Save format and script state.** AngelScript has no built-in object
   serialisation. Game state should live in engine-owned structures wherever
   practical, with scripts holding behaviour rather than authoritative state.
5. **Physics backend order.** The original was explicitly plugin-based over
   PhysX, Bullet and Box2D. Bullet is the natural first backend on licence and
   platform grounds; PhysX 3.4 is the second candidate if CPU cloth and soft
   body fidelity is required. PhysX 5 is not a candidate, because its
   deformable features depend on GPU execution.
6. **Engine name.** OpenGrace follows the convention of OpenMW, OpenRCT2,
   OpenTTD and OpenSAGE, all of which take their name from the original product.
   The closest precedent is OpenSAGE, which reimplements an internal EA engine.

---

## Verification commands

```sh
# Default configuration, no graphics dependency
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure

# With the OpenGL backend
cmake -S . -B build-gl -G Ninja -DCMAKE_BUILD_TYPE=Debug -DGRACE_ENABLE_OPENGL=ON
cmake --build build-gl --parallel
ctest --test-dir build-gl --output-on-failure
ctest --test-dir build-gl -L opengl --output-on-failure

# Release must stay warning-free under -Werror
cmake -S . -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DGRACE_ENABLE_OPENGL=ON
cmake --build build-rel --parallel
```

Boundary checks that must keep passing:

```sh
grep -rn 'SDL\|GL/\|gl\.h\|GLuint' include/grace/core src/core || echo 'core is platform-free'
grep -rn 'GLuint\|GLenum\|SDL_\|Window' include/grace/render/render_backend.hpp | grep -v '^ *//' \
  || echo 'the render contract is API-neutral'
```
