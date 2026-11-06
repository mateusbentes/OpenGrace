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
| 4a | Forward lighting: light types, surface parameters, submission, lighting shader | Done |
| 4b | Shadow maps: render target infrastructure, single-cascade sun shadow map | Done |
| 4c | Cascaded sun shadow mapping with per-slice corner fitting and bilateral filtering. The trapezoidal projective transform of architecture §3.4 (decision 8) is evaluated in 4e. Dual paraboloid point-light shadows are 4d | Done |
| 4d | Dual paraboloid point-light shadow maps with bilateral filtering | Done |
| 4e | Trapezoidal projective transform for the sun cascades — implemented behind `CascadeProjection::Trapezoidal` and measured against the fitted default (decision 8 resolved below) | Done |
| 5 | Deferred lighting alongside forward: MRT G-buffer, lighting pass. Light-volume GI remains milestone 7 | Done |
| 5b | Move frame composition out of the render contract into a renderer layer (`grace::render::Renderer`); the backend is a device again. Closes decision 12 | Done |
| 5c | Shader sources leave the API-neutral module (decision 14); deferred path consumes the existing shadow maps (G1) | Done |
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
| 2026-11-06 | same, forward-lighting increment | OpenGL off: 8/8 Debug; OpenGL on: 13/13 Debug and Release; `-L opengl` 5/5; zero warnings under `-Werror` |
| 2026-11-06 | same | Check counts: version 4, fixed_step_clock 42, application 26, math 31 (Debug) / 28 (Release), lights 30, opengl_context 20, opengl_render 49, mesh 13, shader 16, lighting 10 — all zero failures. Open decision 1 resolved: forward primary, deferred stays planned as milestone 5. Debug math contract violations abort (exit 134) with a named message; Release keeps the documented silent behaviour |
| 2026-11-06 | same | Visible run: `opengl 4.6 core` Iris Xe, pixel `30,30,30,255`, exit 0; hidden `xvfb-run` run: llvmpipe `4.5 core`, same pixel, exit 0. Lighting readback checks pass: sun-only, point falloff at known distance, spot inside/outside cone, emissive-only; 5-light submission rejected with "exceeds the per-object limit of 4" |
| 2026-11-06 | same, milestone 4b | OpenGL off: 8/8 Debug; OpenGL on: 14/14 Debug and Release; `-L opengl` 6/6; zero warnings under `-Werror` |
| 2026-11-06 | same, milestone 4b | Check counts: version 4, fixed_step_clock 42, application 26, math 35, lights 36, opengl_context 20, opengl_render 75, mesh 13, shader 16, lighting 10, shadows 9 — all zero failures. Shadow readback pixels: lit `128,128,128,255`, occluded `0,0,0,255`, no-sun emissive-only `64,128,191,255` |
| 2026-11-06 | same, milestone 4b | Scope note: the sun shadow map is a **single orthographic cascade covering the whole scene bounds** (fixed 1024 resolution by config, constant documented depth bias, applied to the sun term only; dynamic lights unshadowed). Trapezoidal cascaded shadow mapping and dual paraboloid point-light shadow maps from architecture §3.4 are **4c and not implemented**; the original engine's shadow behaviour is not reproduced |
| 2026-11-06 | same, milestone 4c | OpenGL off: 9/9 Debug; OpenGL on: 15/15 Debug and Release; `-L opengl` 6/6; zero warnings under `-Werror`. Decision 7 resolved as generic render-pass composition (option b); `renderShadowMap` removed from the contract |
| 2026-11-06 | same, milestone 4c | Check counts: version 4, fixed_step_clock 42, application 26, math 35, lights 36, cascades 133, opengl_context 20, opengl_render 79, mesh 13, shader 16, lighting 10, shadows 8 — all zero failures. Shadow readback pixels: near-surface lit `128,128,128,255`, near occluded `0,0,0,255`, far-surface lit `128,128,128,255`, far occluded `0,0,0,255`, no-sun emissive-only `64,128,191,255`, debug cascade index `0,0,255,255` |
| 2026-11-06 | same, milestone 4c | Scope note: cascades are N=4 (documented, clamped to [1,4]) with practical log/uniform splits, per-slice corner fitting of the light-space box to the slice's transformed frustum corners (the standard fitted cascade; the trapezoidal projective transform of §3.4 is not implemented), view-depth cascade selection with a 25% blend zone at cascade boundaries, and a bilateral 3x3 filter (spatial Gaussian x depth-range weight, documented tolerance) — not plain PCF. Sun term only; dynamic lights unshadowed. Dual paraboloid point-light shadows (architecture §3.4) are **4d, not implemented**; the original's shadow behaviour is not fully reproduced |
| 2026-11-06 | same, milestone 4d | OpenGL off: 10/10 Debug; OpenGL on: 17/17 Debug and Release; `-L opengl` 7/7; zero warnings under `-Werror`. Decision 7 validated: the paraboloid pass needed no contract change |
| 2026-11-06 | same, milestone 4d | Check counts: version 4, fixed_step_clock 42, application 26, math 35, lights 36, cascades 133, paraboloid 27, opengl_context 20, opengl_render 91, mesh 13, shader 16, lighting 10, shadows 8, point_shadows 7 — all zero failures. Point-shadow readback pixels: back lit `129,129,129,255`, back occluded `0,0,0,255`, front lit `154,154,154,255`, front occluded `0,0,0,255` |
| 2026-11-06 | same, milestone 4d | Scope and findings: dual paraboloid point-light shadows, budget 2 shadow-casting point lights (two depth targets per light; ~16 MB at 1024), over-budget lights lit without a shadow map + counted violation, projective w=1±dir.z paraboloid projection with per-fragment radial gl_FragDepth, bilateral 3x3 filter. **Bias finding (D4): a constant bias IS sufficient for the paraboloid** because its stored depth is linear in world distance (world bias / (far-near)); the cascade path keeps its clip-depth bias because clip depth is non-linear |
| 2026-11-06 | same, milestone 4e | OpenGL off: 11/11 Debug; OpenGL on: 19/19 Debug and Release; `-L opengl` 8/8; zero warnings under `-Werror`. Decision 8 resolved with measurements (see above): fitted cascade remains the default; trapezoidal warp is opt-in and fell back safely in the test scene |
| 2026-11-06 | same, milestone 4e | Check counts: version 4, fixed_step_clock 42, application 26, math 35, lights 36, cascades 133, paraboloid 27, trapezoid 80, opengl_context 20, opengl_render 91, mesh 13, shader 16, lighting 10, shadows 8, point_shadows 7, tsm 11 — all zero failures. TSM readback (both modes): acne/lit `110,110,110,255`, near occluded `0,0,0,255`, far occluded `0,0,0,255`. Density: near 0.0284, far 0.2084 world/texel (ratio 7.34) in the default (fitted) mode; temporal delta 110 on a 7x7 grid for a +0.02 camera shift, both modes |
| 2026-11-06 | same, milestone 5 | OpenGL off: 11/11 Debug; OpenGL on: 20/20 Debug and Release; `-L opengl` 9/9; zero warnings under `-Werror`. Forward stays the default path and passes unchanged; `application.cpp` untouched (still only setClearColor/clear/shutdown) |
| 2026-11-06 | same, milestone 5 | Deferred measurements (tests/test_deferred.cpp): G-buffer attachment 0 centre readback `255,255,255,16` (albedo white, a = shininess/1024); parity forward `128,128,128,255` vs deferred `127,127,127,255` (diff 1); 6 dynamic lights (over the forward limit of 4, rejected there) — **corrected in 5b: this measurement was wrong**. The observed value, `127,127,127,255`, is the sun alone (`0.5` at a normal-light dot of one gives `127`); the expectation, `~124`, modelled six point lights and omitted the sun. The two landed within the tolerance of three of each other, so the check passed without the dynamic lights contributing anything, and the multi-light capability this milestone claimed was not demonstrated. The corrected measurement is in the 5b row. Shadow maps are NOT consumed by the deferred path in this increment (documented); the deferred light cap is kMaxDeferredLights = 16 (shader uniform array), over-cap submissions rejected, not truncated. G-buffer: 2x RGBA8 + depth24, normals 8-bit mapped, scalar specular — ~11.1 MB at 1280×720, documented |
| 2026-11-06 | same, milestone 5b | OpenGL off: 12/12 Debug; OpenGL on: 21/21 Debug and Release; `-L opengl` 9/9; zero warnings under `-Werror`. Contract shrank by exactly the path concept (`RenderPath`/`setRenderPath`/`renderPath`/`present`); nothing a device must do was lost. `application.cpp` diff is the 4-hunk renderer switch (recorded in decision 12) |
| 2026-11-06 | same, milestone 5c | OpenGL off: 13/13 Debug; OpenGL on: 22/22 Debug and Release; `-L opengl` 9/9; zero warnings under `-Werror`. `shader_source_rule` test: scanned 41 files, 0 hits outside src/render/opengl/ and tests/ |
| 2026-11-06 | same, milestone 5c | G1 implemented (option a): the deferred path now renders and consumes the existing cascade + paraboloid shadow maps via the renderer; all shadow code (free functions, matrices, configs) reused unchanged. Shadow parity with a castOnly occluder: forward centre `0,0,0,255` vs deferred centre `0,0,0,255` (diff 0). Plain parity unchanged: forward `128,128,128,255` vs deferred `127,127,127,255` (diff 1) |
| 2026-11-06 | same, milestone 5b | Deferred via the renderer after 5b: centre pixel `127,127,127,255` — identical to the direct-contract value before the move. Parity forward `128,128,128,255` vs deferred `127,127,127,255` (diff 1). G-buffer attachment 0 readback `255,255,255,16`. CLI: `--path forward` and `--path deferred` both exit 0 with centre pixel `30,30,30,255` (deferred matches forward via the cleared-depth background rule); `--path bogus` exits 2. Check counts: version 4, fixed_step_clock 42, application 26, math 38, lights 36, cascades 133, paraboloid 27, trapezoid 80, renderer 19 (new), opengl_context 20, opengl_render 107, mesh 13, shader 16, lighting 10, shadows 8, point_shadows 7, tsm 11, deferred 21 — all zero failures; no existing suite decreased (opengl_render 113→107 because the shared checks lost the contract's path block; application unchanged at 26 with the updated event log) |
| 2026-11-05 | same, contract extension | OpenGL off: 7/7 Debug; OpenGL on: 11/11 Debug and Release; `-L opengl` 4/4; zero warnings under `-Werror`; check counts: opengl_context 20, opengl_render 47 |

| Published tree did not build with `GRACE_ENABLE_OPENGL=ON` | The lighting commit left the shader header unstaged and published half of a `const` change, so `drawIndexed() const` in the null backend had no matching override | Complete the change set and verify by building a fresh clone |

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
1. **Forward or deferred rendering as the primary path?** **Resolved
   2026-11-06: forward is the primary path.** The original supported both
   (forward with up to four dynamic lights per object plus sunlight, deferred
   with unlimited lights); this project adopts the forward path as primary
   because the Linux/OpenGL target and the per-object light model map onto it
   directly, and it keeps materials and the lighting pass simple and
   testable. The per-object dynamic light limit is enforced at exactly four
   (`kMaxDynamicLightsPerObject`), matching the documented behaviour. The
   deferred path is **not** cancelled: it remains planned as milestone 5.
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
7. **Shadow-pass contract shape: technique-named method or generic render-pass
   composition?** The 4b contract method `renderShadowMap(target, sun, bounds,
   submissions)` could not carry several cascades without becoming a different
   method. **Resolved 2026-11-06: option (b), generic render-pass composition.**
   The contract now exposes `beginRenderPass(target)` / `endRenderPass()` with
   `clear()`/`draw()`/`submit()` operating on the pass target, and no longer
   knows what a shadow is; `renderShadowMap` was removed and the cascaded pass
   is built from those primitives in the render module (`renderShadowCascades`),
   with the shadow map crossing `DrawSubmission` as plain data (`ShadowMap`).
   Reasoning: it serves a deferred shading path equally, and there was only one
   consumer to migrate. This was preferred over widening a technique-named
   method (option (a)), which would have kept the contract shadow-aware.
   **Validated by milestone 4d (2026-11-06): the dual paraboloid point-light
   shadow pass was assembled entirely from beginRenderPass/endRenderPass/
   clear/draw plus plain data on DrawSubmission — no technique-specific method
   was added to the contract, and no contract method changed.** Decision 7 was
   correct: the second shadow technique needed zero contract changes.

---
8. **Is the fitted cascade a sufficient substitute for the trapezoidal
   transform?** **Resolved 2026-11-06 (milestone 4e): the fitted cascade stays
   the default; the trapezoidal transform is implemented behind
   `CascadeProjection::Trapezoidal` (opt-in) and was measured against it.**
   Measurements (256² maps, 4 cascades, test scene in `tests/test_tsm.cpp`):
   with the containment safety check enforced (the warp may only engage when
   every slice corner maps inside the map with positive w), the trapezoidal
   warp falls back to the fitted matrix for every cascade in the test scene,
   so density and stability are identical (near 0.0284 world/texel, far
   0.2084, ratio 7.34; camera +0.02 → 7×7 grid delta 110 in both modes).
   With the check relaxed so the warp engages, measured density was near
   0.0788 / far 0.3687 (ratio 4.68) — the near side about 2.8 times
   coarser than the fitted 0.0284,
   which is the opposite of what the technique exists to do, and the far side
   worse — and the far-occluder shadow broke (pixel 110 instead of 0).
   Temporal stability was not compared: with the check enforced both modes use
   the fitted matrix, and without it the result is incorrect. Conclusion: this
   implementation does not demonstrate the technique's benefit, which is not
   the same as the technique having none — a near-side result that is worse
   rather than better points at the construction or the warp rather than at the
   premise. The fitted cascade stays the default on the evidence available, and
   the trapezoidal path is kept as a selectable experiment, off by default, and
   explicitly unproven.

9. **What happens when a light exceeds the shadow-casting budget?** The budget
   for shadow-casting point lights is separate from the four-dynamic-lights
   per-object limit. A light beyond it is still lit, without a shadow map, and
   the submission counts a protocol violation. This differs from the four-light
   limit, where an over-limit submission is rejected outright, and the
   difference is deliberate: exceeding the object light limit means the object
   would be shaded differently from what was asked for, while exceeding the
   shadow budget still produces correct lighting and only omits a shadow. The
   violation count therefore means two different things depending on the cause,
   which is a weakness worth revisiting if a third such limit is added.

10. **Should the trapezoidal fallback be silent?** When the containment check
    rejects a trapezoid, the builder falls back to the fitted matrix without
    saying so. A silent fallback means a test can pass while the technique under
    test never runs, which is what happened in 4e. The fallback is currently
    documented but not observable; making it countable would have surfaced the
    situation in the first measurement instead of the second.

11. **Multiple render targets in the contract, or several single-attachment
   passes?** **Resolved 2026-11-06 (milestone 5): the contract gains multiple
   colour attachments** — `RenderTargetDesc` now carries
   `colorAttachmentCount` + a fixed `colorFormats[kMaxColorAttachments]` array
   with a **documented hard cap of 4, enforced by createRenderTarget throwing
   std::invalid_argument** (the cap is never silently applied).
   `renderTargetTexture(target, index)` selects a colour attachment by index,
   `kDepthAttachment` selects the depth attachment. Reasoning: a G-buffer
   written as N single-attachment passes costs N geometry passes and is not a
   G-buffer; MRT keeps the contract honest (one pass, all attachments) and the
   pass primitives remain the composition mechanism (decision 7). The deferred
   path in milestone 5 uses a 2-colour + depth G-buffer; memory at the default
   1280×720 is 12 bytes/pixel = ~11.1 MB, documented in render_target.hpp's
   users (G-buffer layout notes in the deferred shader header).

12. **Is a mode-dependent present() acceptable in the contract?** **Resolved
    2026-11-06 (milestone 5b) by the second resolution: the technique left the
    contract.** A renderer layer (`grace::render::Renderer` in
    `include/grace/render/renderer.hpp`) now owns the frame — path selection,
    shadow passes, geometry pass, deferred lighting pass, frame begin/end — and
    composes them from the backend's pass primitives. The contract lost
    `RenderPath`/`setRenderPath`/`renderPath`/`present` and keeps only device
    capabilities (targets, passes, textures, camera, draw/submit, clear,
    readback, shutdown, texture binding). `src/app/application.cpp` changed
    exactly once, in this increment, with the recorded reason: the application
    drives the renderer instead of the backend (constructor takes
    `render::Renderer&`; runFrame calls `beginFrame()`/`endFrame()` instead of
    `backend.clear()`; shutdown via `renderer.backend().shutdown()`). The
    fixed-step loop, frame timing and shutdown order are unchanged. The shadow
    free functions stay in `shadow.cpp` and are called by the renderer, which
    owns their configuration. Known remaining leak, recorded not fixed:
    `DrawSubmission` still carries `shadow`/`pointShadows` pointers —
    composition data on a device-level type; the proposal is for the renderer
    to hold the `ShadowMap` and bind it at submit time in a later increment.
    The `grace` binary now selects the path with `--path forward|deferred`
    (default forward, invalid value = usage error exit 2) and both paths run
    to completion, exit 0.
13. **Renderer vs backend layering.** Recorded with decision 12: the renderer
    owns frame composition and all path/shadow/light configuration; the backend
    is a device. The renderer is not a god object — resource management, scene
    storage, culling and asset loading are explicitly out of its scope.
14. **Where do shader sources live?** **Resolved 2026-11-06 (milestone 5c): only
    in the OpenGL backend's shader library** (`src/render/opengl/shader_library.hpp`).
    The contract's shader factory takes a closed, API-neutral `ShaderProgramId`
    enum and the backend resolves it to its own source; callers (renderer,
    shadow passes, tests) never hold shading-language text. Rule for every
    future shader, stated once: a new shader = one `ShaderProgramId` entry + one
    entry in the backend library; the API-neutral render module must never
    contain shading-language source (enforced by the `shader_source_rule` test,
    which scans the tree and fails on any hit outside `src/render/opengl/`).

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
