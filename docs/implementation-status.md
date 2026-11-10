# OpenGrace — Implementation Status

Running record of milestone progress, validation results and open decisions.
The architectural baseline is [grace-architecture.md](grace-architecture.md).

The `.docx` files in this directory are generated from the `.md` sources with
`pandoc` and are committed for convenience. Edit the Markdown, then regenerate:
`pandoc docs/implementation-status.md -o docs/implementation-status.docx`.

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
| 5 | Deferred lighting alongside forward: MRT G-buffer, lighting pass (decision 11). Light-volume GI remains milestone 7 | Done |
| 5b | Move frame composition out of the render contract into a renderer layer (`grace::render::Renderer`); the backend is a device again. Closes decision 12 | Done |
| 5c | Shader sources leave the API-neutral module (decision 14); deferred path consumes the existing shadow maps (G1) | Done |
| 6a | HDR pipeline: float HDR target, bloom, filmic tonemap + sRGB display encode, owned by the renderer, off by default | Done |
| 6b | Ambient (indirect) lighting term in both paths (decision 17 — a documented deviation from the documented light types) | Done |
| 6c | SSAO: occlusion factor in a second HDR attachment (ambient only), depth-gradient normals, darkens the ambient term at the tonemap (decision 18). SSAO-STF temporal filtering deferred to its own increment (6g, decision 31) | Done |
| 6d | Base repair (decision 20: fullscreen passes never write depth; HDR depth = scene depth in both paths) + ambient default 0.2 (decision 21) + DOF with bokeh and morphological AA (decision 22 chain order), both off by default | Done |
| 6e | Post chain on by default: the chain became the product once its techniques landed (decision 23). Every measured value re-baselined with before/after figures, no tolerance widened | Done |
| 6f | Volumetric lights as an analytic region — a sphere around a point light, a cone around a spot light, through a `volumeDensity` property defaulting to off (decision 25) | Done |
| 6g | SSAO with selective temporal filtering (SSAO-STF), camera-only scope (decision 31): the scene is static, so the history is validated by camera reprojection — the current pixel's world position is projected with the previous frame's view-projection matrix, and the history is rejected when the reprojected position falls outside the screen or the history's depth disagrees with the current depth | Done |
| 6 | Post-processing: HDR, bloom, filmic tonemap (6a), ambient term (6b), SSAO (6c), DOF + morphological AA (6d) — all documented in section 3.2; SSAO-STF temporal filtering landed in its own increment (6g) | Done |
| 7 | Global illumination by light volume propagation. Four increments, all measured: the 3D texture exists in the contract and in both backends, a pass renders into one layer of a volume without touching the others, the scene deposits its light into every layer, one propagation step averages the six face neighbours, and the volume composes into the frame — the ambient term of both paths (decision 30, decision 17(d)) | Done |
| 8 | Physics contract and the first backend: the contract (8a), the null backend (8b), and the Bullet, Box2D and PhysX 3.4 adapters (8c) (decision 32) | Done |
| 9 | Script host: the contract (9a), the null backend (8b) and the AngelScript adapter (9b) (decision 33); serialisation strategy landed in milestone 12b (decision 36) | Done |
| 10 | Visual scripting: block registry and graph evaluation — the contract (10a), the null backend (10a) and the adapter (10b) (decision 34) | Done (10a, 10b) |
| 10c | The OWD resource container: the writer, the reader, the filters, the checksums, the overlay loader (11a) and the `owdtool` command line (11b), implementing the specification the decisions 24 and 26 to 29 shaped (decision 35) | Done |
| 11 | Animation: skeletal clips, the deterministic sampler (11a, 11b), the state-machine layer with the cross-fade (11c) and the look-at and foot placement controllers (11c) (decision 38); blendshapes in 11a | Done |
| 12 | Scene management: data-driven entities, selectable spatial partitioning (flat, grid, octree, quadtree), scene queries and LOD (12a), plus the serialisation strategy writing the scene's state into the OWD container (12b) (decision 36) | Done |
| 13 | Audio: 2D/3D playback, streaming, reverb -- the contract (13a), the deterministic null backend (13b) and the OpenAL Soft adapter with the WAV and Ogg Vorbis decoders (13c) (decision 37) | Done |
| 14 | UI: retained controls, the UTF-8 event path and the font contract (14a), the deterministic null backend (14b) and the FreeType adapter behind GRACE_ENABLE_FREETYPE (14c) (decision 39) | Done |
| 15 | AI and gameplay services: the agent behaviours (idle, patrol, seek, flee) with the deterministic steering, the timers, the named properties, the event queue and the seeded random source (15a, 15b) (decision 40) | Done |
| 16 | Video: the playback service -- the contract (16a), the deterministic null backend (16b) and the Ogg Theora decode layer behind GRACE_ENABLE_THEORA (16c) (decision 41) | Done |
| 17 | Windows validation: the MinGW-w64 cross-compile builds the tree clean under -Werror (105 targets), the OpenGL backend links opengl32.dll and the Windows system libraries, the GL headers load glcorearb.h, and the SDL teardown releases the video subsystem by reference count with the context unbound before deletion (decision 42) | Done |
---

## Module status

| Module | State | Notes |
| --- | --- | --- |
| core | Partial | Version, fixed-step clock, header-only math (vec/mat4/transform/camera). Dependency-free, enforced by grep in review. |
| app | Partial | Application lifecycle driver and the CLI entry point. |
| render | Partial | Contract, null backend, OpenGL backend, loader, API-neutral mesh/shader/material plus their OpenGL implementations; diagnostic triangle rebuilt on the foundation. |
| physics | Partial | Contract (8a), null backend (8b), and the Bullet, Box2D and PhysX 3.4 adapters (8c) behind build options. PhysX is the extra-physics capability (decision 32); the classical layer is backend-neutral. |
| owd | Partial | The OWD container (11a): the writer, the reader, the filters, the checksums and the overlay loader, plus the `owdtool` command line (11b), on zlib's Deflate. |
| script | Partial | The contract (9a), the null backend (9b) and the AngelScript adapter behind GRACE_ENABLE_ANGELSCRIPT; the visual block system (10a, 10b) registers through the same surface. |
| scene | Partial | Data-driven entities, selectable spatial partitioning (flat, grid, octree, quadtree), scene queries and LOD (12a); the serialisation strategy writes the scene's state into the OWD container (12b) (decision 36). |
| audio | Partial | The contract (13a), the deterministic null backend (13b) and the OpenAL Soft adapter behind GRACE_ENABLE_OPENAL (13c), with hand-written WAV and libvorbisfile Ogg Vorbis decoders in the data layer. |
| motion | Partial | Skeletal clips, the deterministic sampler with lerp/slerp and FK, the blendshapes, the events, the state-machine layer with the cross-fade and the look-at and foot placement controllers (decision 38). |
| gui | Partial | The retained control tree, the hit test and focus, the event dispatch with the UTF-8 text path and the font contract (14a); the deterministic null backend (14b) and the FreeType adapter behind GRACE_ENABLE_FREETYPE (14c) (decision 39). |
| ai, game | Partial | The agent behaviours (idle, patrol, seek, flee) with the deterministic steering and the gameplay-level services: timers, named properties, the event queue and the seeded random source (decision 40). |
| video | Partial | The playback service: the frame-stepper contract (16a), the deterministic null backend (16b) and the Ogg Theora decode layer behind GRACE_ENABLE_THEORA (16c) (decision 41). |

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
| 2026-11-06 | same, milestone 4b | OpenGL off: 8/8 Debug; OpenGL on: 14/14 Debug and Release; `-L opengl` 6/6; zero warnings under `-Werror` |
| 2026-11-06 | same | Visible run: `opengl 4.6 core` Iris Xe, pixel `30,30,30,255`, exit 0; hidden `xvfb-run` run: llvmpipe `4.5 core`, same pixel, exit 0. Lighting readback checks pass: sun-only, point falloff at known distance, spot inside/outside cone, emissive-only; 5-light submission rejected with "exceeds the per-object limit of 4" |
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
| 2026-11-06 | same | Check counts: version 4, fixed_step_clock 42, application 26, math 31 (Debug) / 28 (Release), lights 30, opengl_context 20, opengl_render 49, mesh 13, shader 16, lighting 10 — all zero failures. Open decision 1 resolved: forward primary, deferred stays planned as milestone 5. Debug math contract violations abort (exit 134) with a named message; Release keeps the documented silent behaviour |
| 2026-11-06 | same, milestone 5 | OpenGL off: 11/11 Debug; OpenGL on: 20/20 Debug and Release; `-L opengl` 9/9; zero warnings under `-Werror`. Forward stays the default path and passes unchanged; `application.cpp` untouched (still only setClearColor/clear/shutdown) |
| 2026-11-06 | same, milestone 5 | Deferred measurements (tests/test_deferred.cpp): G-buffer attachment 0 centre readback `255,255,255,16` (albedo white, a = shininess/1024); parity forward `128,128,128,255` vs deferred `127,127,127,255` (diff 1); 6 dynamic lights (over the forward limit of 4, rejected there) — **corrected in 5b: this measurement was wrong**. The observed value, `127,127,127,255`, is the sun alone (`0.5` at a normal-light dot of one gives `127`); the expectation, `~124`, modelled six point lights and omitted the sun. The two landed within the tolerance of three of each other, so the check passed without the dynamic lights contributing anything, and the multi-light capability this milestone claimed was not demonstrated. The corrected measurement is in the 5b row. Shadow maps are NOT consumed by the deferred path in this increment (documented); the deferred light cap is kMaxDeferredLights = 16 (shader uniform array), over-cap submissions rejected, not truncated. G-buffer: 2x RGBA8 + depth24, normals 8-bit mapped, scalar specular — ~11.1 MB at 1280×720, documented |
| 2026-11-06 | same, milestone 5b | OpenGL off: 12/12 Debug; OpenGL on: 21/21 Debug and Release; `-L opengl` 9/9; zero warnings under `-Werror`. Contract shrank by exactly the path concept (`RenderPath`/`setRenderPath`/`renderPath`/`present`); nothing a device must do was lost. `application.cpp` diff is the 4-hunk renderer switch (recorded in decision 12) |
| 2026-11-06 | same, milestone 5b | Deferred via the renderer after 5b: centre pixel `127,127,127,255` — identical to the direct-contract value before the move. Parity forward `128,128,128,255` vs deferred `127,127,127,255` (diff 1). G-buffer attachment 0 readback `255,255,255,16`. CLI: `--path forward` and `--path deferred` both exit 0 with centre pixel `30,30,30,255` (deferred matches forward via the cleared-depth background rule); `--path bogus` exits 2. Check counts: version 4, fixed_step_clock 42, application 26, math 38, lights 36, cascades 133, paraboloid 27, trapezoid 80, renderer 19 (new), opengl_context 20, opengl_render 107, mesh 13, shader 16, lighting 10, shadows 8, point_shadows 7, tsm 11, deferred 21 — all zero failures; no existing suite decreased (opengl_render 113→107 because the shared checks lost the contract's path block; application unchanged at 26 with the updated event log) |
| 2026-11-06 | same, milestone 5c | OpenGL off: 13/13 Debug; OpenGL on: 22/22 Debug and Release; `-L opengl` 9/9; zero warnings under `-Werror`. `shader_source_rule` test: scanned 41 files, 0 hits outside src/render/opengl/ and tests/ |
| 2026-11-06 | same, milestone 5c | G1 implemented (option a): the deferred path now renders and consumes the existing cascade + paraboloid shadow maps via the renderer; all shadow code (free functions, matrices, configs) reused unchanged. Shadow parity with a castOnly occluder: forward centre `0,0,0,255` vs deferred centre `0,0,0,255` (diff 0). Plain parity unchanged: forward `128,128,128,255` vs deferred `127,127,127,255` (diff 1) |
| 2026-11-06 | same, milestone 6a | OpenGL off: 13/13 Debug; OpenGL on: 23/23 Debug and Release; `-L opengl` 10/10; zero warnings under `-Werror`. Post off: all existing pixel values unchanged (e.g. 0.5 linear -> 128 legacy). Post on: tonemap response 0.0->0, 0.18->141, 0.5->206, 1.0->232, 2.0->245, 4.0->252; bloom halo 61 @4px (0 when off); deferred+post 0.5 -> 206 (same as forward). CLI four combinations (forward/deferred x post off/on) all exit 0: 30,30,30,255 with the chain off and 110,110,110,255 with it on, in both paths. Measured after the presentation fix in the same commit; before it every combination read 30,30,30,255, because clear() captured the pixel and swapped the buffer before the tonemap ran. shader_source_rule: 0 hits outside the backend library. HDR target: Rgba16F + sampleable depth texture, ~7.4 MB colour at 1280x720 |
| 2026-11-06 | same, milestone 6b | OpenGL off: 13/13 Debug; OpenGL on: 23/23 Debug and Release; `-L opengl` 10/10; zero warnings under `-Werror`. Ambient term in both paths, same math; renderer config `ambient` default 0.05 linear |
| 2026-11-06 | same, milestone 6b | Defect fix with real before/after: occluded centre was `0,0,0,255` in both paths, now `13,13,13,255` in both (ambient 0.05 * albedo 1, legacy 8-bit) — test_deferred shadow parity diff 0. Parity with ambient: forward `140,140,140,255` = deferred `140,140,140,255` (diff 0; before: 128 vs 127). Unlit surface ambient check: 0.1 linear -> pixel 25. Post chain: clear colour 30 linear reads 30 legacy, 110 through the chain (both paths, all four CLI combos exit 0) |
| 2026-11-06 | same, milestone 6c | OpenGL off: 13/13 Debug; OpenGL on: 24/24 Debug and Release; `-L opengl` 11/11; zero warnings under `-Werror`. New suite test_ssao 9/9: flat surface ssao on 232 = off 232 (no self-occlusion); corner (ground + two walls at x/y=0.35) near-corner pixel 206 (occ 0.5, 4/8 samples) vs open-area 232; path agreement on the corner forward 206 = deferred 206 (diff 0); direct-only scene (ambient 0) ssao on 206 = off 206. Post off: all legacy pixel values bit-identical (CLI 30,30,30,255 both paths, exit 0); post on: CLI 110,110,110,255 both paths, exit 0, visible and xvfb hidden |
| 2026-11-06 | same, milestone 6d | OpenGL off: 13/13 Debug; OpenGL on: 25/25 Debug and Release; `-L opengl` 12/12; zero warnings under `-Werror`. Base repair: contract +setDepthTestEnabled/+copyDepthAttachment (null events regenerated, opengl_render 125 checks; violation count 11 -> 13 for the 2 new depth-copy misuse checks). test_ssao 11/11 incl. the new HDR-depth proof: after a full frame, HDR depth readback forward 0.9045 = deferred 0.9045 (quad depth would be 0.5); occlusion values unmoved (corner 206 vs open 232, paths 206=206) — defect fix, not a re-baseline. Ambient default 0.05 -> 0.2 (decision 21): test_deferred occluded parity 13 -> 51,51,51,255 both paths (diff 0), lit parity 140 -> 178; test_post deferred+post 210 -> 220 |
| 2026-11-06 | same, milestone 6d | New suite test_dof_mlaa 40/40: DOF edge row (ground split at x=0, sun 0.5 + ambient 0.2, hard edge 44/220 at column 64): off and focus=1.0 identical (sharp plane at focus); focus=0.5 (CoC clamped 12 px) blends the edge 220->195 at x=64, 44->82 at x=54 (10 px out), x=50 (14 px) and x=76 untouched — the disc cutoff at the 12 px CoC, not a Gaussian decay; deferred row matches forward within 1 (195 182 163 133 133 83 45 45 201 209 215 220). MLAA edge row (ambient-only, dark 12 / bright 149): off hard step, on the two straddling pixels blend to ~103, interiors unchanged. Both techniques off: flat ambient-1.0 pixel 232 = pre-6d tree. CLI 16 visible + 8 xvfb combos (paths x post off/on x technique flags) all exit 0: 30,30,30,255 post off, 110,110,110,255 post on (centre sample is background, so technique flags do not move it) |
| 2026-11-06 | same, milestone 6e | OpenGL off: 13/13 Debug; OpenGL on: 25/25 Debug and Release; `-L opengl` 12/12; zero warnings under `-Werror`. Structural change (decision 22 update): the chain ends in an offscreen Rgba8 display target; MLAA runs after the tonemap on the display-encoded image (edge blend of codes 12/149 now 80, was 103 in the linear domain); present = new PostBlit pass onto the default framebuffer. test_post 16/16 adds: default-config frame reports centre 110 (derived sRGB(aces(30/255))*255 = 110.51, GL 110) and the present proof (default-FB captured pixel == displayTarget() pixel). CLI: no args -> 110,110,110,255 both paths, visible and xvfb, exit 0; --no-post -> 30,30,30,255 (the two paths differ) |
| 2026-11-06 | same, milestone 6e | Re-baseline table (old legacy -> new chain, equation round(255*sRGB(aces(x)))): test_deferred D4 parity centre 178 -> 220 (x=0.7, relative check diff 0 unchanged in form); 6-lights centre 253 -> 231 (x~0.9922); shadow parity occluded 51 -> 149,149,149,255 both paths (diff 0; x=0.2 ambient); test_renderer forward-frame event log 7 -> 117 events (full chain, regenerated literal); test_application events_at_step {1,1,5,5,9,9} -> {1,1,115,115,211,211} with structural anchors (3 setCamera, 6 clears, 3 present passes, shutdown last, violations 0). Unchanged: all direct-backend suites (lighting/shadows/point_shadows/tsm/opengl_render — they never run the chain), test_post explicit on/off values, test_ssao 206/232/232 + HDR depth 0.9045, test_dof_mlaa DOF row. Category: default change (decision 23), NOT defect fixes and NOT re-baselines-for-convenience; no tolerance was widened. 6e also fixed a real defect the flip exposed: forward + shadows + chain drew lighting into the default FB because shadow passes end there (renderer now re-opens the lighting pass after shadow passes) — before the flip this was masked because post-off targeted the default FB anyway |
| 2026-11-06 | same, milestone 6f | OpenGL off: 13/13 Debug; OpenGL on: 26/26 Debug and Release; `-L opengl` 13/13; zero warnings under `-Werror`. New suite test_volumetric 14/14 with derived expectations: sphere volume centre ray — march mirrored in C++ gives v=0.04163, expected chainPixel(0.2+v)=round(255*sRGB(aces(0.24163)))=162, measured 162; exposure 2 -> derived 204, measured 204 (volume responds to exposure); camera inside the sphere (light at the camera): t0 clamped to 0, derived v=0.02588 -> 157, measured 157; spot cone: derived v=0.05056 + surface light 0.02439 -> 171, measured 171, and outside the outer cone (x=112) the pixel stays 149; shadow along the ray: unshadowed 182 vs occluder 177 (paraboloid tested per sample; pointShadows.near=0.02 so the occluder 0.05 from the light is inside the map); path agreement forward 162 = deferred 162 (diff 0); over-cap (5 volumetric lights, deferred) rejected with invalid_argument; default off: density-0 light 149 = no light 149. No contract change (composed from pass primitives; PostVolumetric is one id + one library entry). Null-backend logs: first frame +2 creation events (m_vol target, PostVolumetric program) and +4 per-frame tonemap events (volTex bind + uniforms) — test_application events_at_step {1,1,115,115,211,211} -> {1,1,121,121,221,221} with the derivation updated (120 = 100 steady-state + 20 one-time creations); test_renderer forward literal regenerated (123 events). CLI unchanged: 110,110,110,255 no args (both paths, visible + xvfb, exit 0), 30,30,30,255 with --no-post — the diagnostic frame has no lights, so the effect does not move the CLI pixel |
| 2026-11-06 | same, milestone 6c | Check counts (all zero failures): version 4, fixed_step_clock 42, application 26, math 38, lights 36, cascades 133, paraboloid 27, trapezoid 80, shader_source_rule 1, renderer 26, opengl_context 20, opengl_render 125, mesh 13, shader 15, lighting 11, shadows 8, point_shadows 7, tsm 11, deferred 23, post 16, ssao 11, dof_mlaa 40, volumetric 14, application 30. No existing suite decreased (opengl_render grew 115 -> 125 for the new contract checks; ssao 9 -> 11 for the depth proof). shader_source_rule: `#version` only in `src/render/opengl/shader_library.hpp`. `application.cpp` unchanged (git diff empty). R8 occlusion buffer ~1 B/pixel; HDR target 2x Rgba16F + depth (~16.6 MB colour at 1280x720) |
| 2026-11-05 | same, contract extension | OpenGL off: 7/7 Debug; OpenGL on: 11/11 Debug and Release; `-L opengl` 4/4; zero warnings under `-Werror`; check counts: opengl_context 20, opengl_render 47 |
| 2026-11-07 | same, container specification | Index blocked and dictionary declined, both measured before being written. The index as one deflated stream against blocks of 256 records: a lookup is 0.4 ms at 10,000 entries and 32.9 ms at 400,000, against 12 us at every size in blocks; the block table is 12 bytes per block, a flat 0.5% of the index at every size measured, 18,756 bytes at 400,000 entries against the 3,513,508 it replaces. Header 28 -> 32 bytes for the block count. The dictionary's first measurement is retracted: its payloads differed in 1.0% of their bytes because the generator never read its seed, so the dictionary contained the payload it was measured against. Re-measured on payloads whose geometry differs, the dictionary makes them 1.8-1.9% larger, gains 0.0% over the filter, and gains 3.2% on an LOD chain, capped at 4.0% by the 32768-byte window. Also corrects a contradiction in section 4.6, whose prose described record transposition (43.1%) where its table measures byte transposition (34.9%). Tools: tools/measure-owd.py, tools/measure-dict.py |
| 2026-11-07 | same, container specification | Payload blocked so requirement 2 holds in the middle of an entry, not only at its start. One deflate stream cannot be entered part way, so serving a 64 KB read in the middle of an 8.0 MiB vertex buffer inflates 8,400,000 bytes and takes 6.5 ms; with the payload blocked it is 131,072 bytes and 231 us, 64 and 28 times less, and on a 16 MB texture 14.4 ms against 223 us. The payload now uses the index's shape: a payload block table and independently deflated blocks of 256 KiB, reduced to a multiple of the element size. Filtering per block costs +21.55% on the vertex buffer and +91.60% on an index buffer, which is 2.6% of the payload in bytes. No record field was added: block size and count are derived. Sections 4.6.1 and 4.8. Measured by `tools/measure-stream.py` |
| 2026-11-07 | same, both documents under the rules | OpenGL off: 14/14 Debug; OpenGL on: 27/27 Debug and Release; -L opengl 13/13; docs_structure 16 checks, 0 failures in both trees; zero warnings under -Werror |
| 2026-11-07 | same, milestone 7 second increment | A pass renders into one layer of a volume, and only that layer. OpenGL off: 15/15 Debug; OpenGL on: 28/28 Debug and Release; -L opengl 13/13; docs_structure 17 checks, 0 failures; texture3d 23 -> 41 checks with no GL context; opengl_render 157 -> 193 checks; zero warnings under -Werror. Delta measured suite by suite against f5e84a8: +54 in total, +18 in texture3d and +36 in opengl_render, and no other suite changed. Measured on the driver before the code was written: attaching layer 1 of a 4x4x3 volume is GL_FRAMEBUFFER_COMPLETE for Rgba8, Rgba16F and R8 with no GL error on create, attach, clear or read; clearing through the target changes layer 1 and leaves layers 0 and 2 untouched |
| 2026-11-07 | same, milestone 7 third increment | The scene deposits its light into the volume. `test_volume_inject` reads the volume back and compares against the surface shader's attenuation: texel(0,0) of layer 0 measures 5.77344 against 5.77600 predicted (one half-float quantum), layer 3 measures 0.37793 against 0.37793, and a bright light outside its radius deposits exactly 0. OpenGL on: 29/29 Debug and Release, 14/14 labelled opengl; the new suite reports 26 checks, 0 failures. No existing count decreased; `test_opengl_render` stays at 193. The contract grew 36 -> 37 pure virtual methods (`releaseTexture3D`, the gap the first increment left), the shader library 24 -> 25 and `ShaderProgramId` 16 -> 17; `gl_loader.cpp` stays at 65, since no new GL entry point is loaded. The test total was first written as 27/27, which counted the suites and not the three command-line tests registered alongside them. Off by default: a frame that asks for no volume creates none and changes no pixel |
| 2026-11-07 | same, milestone 7 fourth increment | One propagation step averages the six face neighbours, measured cell by cell against the average the injection wrote: the volume is 4x4x4 and every cell is checked, and the far corner cell (which the injection filled with 0.08162) holds 0.10120 after the step — the light spread into the cells the injection did not light. OpenGL on: the new suite reports 88 checks, 0 failures. No existing count decreased; `test_opengl_render` stays at 193. The shader library went from 25 to 26 entries and `ShaderProgramId` from 17 to 18, both measured with the methods the earlier entries declare; the render contract stays at 37 pure virtual methods and `gl_loader.cpp` stays at 65 — no new GL entry point, because the propagation renders through the layer target the second increment added |
| 2026-11-07 | same, milestone 7 first increment | 3D texture in the contract, both backends and the null backend. OpenGL off: 15/15 Debug; OpenGL on: 28/28 Debug and Release; -L opengl 13/13; docs_structure 17 checks, 0 failures; texture3d 23 checks, 0 failures with no GL context; opengl_render 125 -> 157 checks; zero warnings under -Werror. Measured on the driver before the code was written: a Depth24Stencil8 volume is refused by glTexImage3D with GL_INVALID_OPERATION, and an R8 3x3 slice round-trips with row alignment 1 and not with 4 |
| 2026-11-08 | same, milestone 7 fourth increment | The volume composes into the frame: the lighting pass samples the volume at the fragment's world position and adds the sampled radiance to the ambient term (decision 17(d)), in both paths. One point light inside the volume, one ground quad, the centre pixel measured with compose on and off: forward 149,149,149,255 (off) and 205,205,205,255 (on); deferred 149,149,149,255 (off) and 205,205,205,255 (on) — the two paths agree, which is what decision 17(d) demands. The compose responds to exposure (2.0 gives 231,231,231,255) and to the legacy path (post off: 51,51,51,255 off, 124,124,124,255 on). `test_volume_compose` reports 19 checks, 0 failures: the numeric half on OpenGL, the structural half on the null backend (a compose flag on a disabled volume is rejected, the submit record carries volume=1 when compose is on, and the volume passes run before the lighting submit). The same commit builds the cross-reference rule (R13): a source line naming a milestone and a decision must name a decision the milestone's row records — 14 of the 71 decision references in the source name a milestone and seven of those were wrong, all caught by the rule; the rule reads include/, src/ and tests/ (a scope change from the two documents the rules read before), verified against the real tree (23 checks on docs_structure, 0 failures) and by an injected defect that the rule catches. OpenGL off: 15/15 Debug; OpenGL on: 31/31 Debug and Release; `-L opengl` 16/16; docs_structure 17→23 checks, 0 failures; zero warnings under -Werror. No existing count decreased; the shader library stays at 26 entries and ShaderProgramId at 18, the contract stays at 37 pure virtual methods, and gl_loader.cpp stays at 65 — no new GL entry point, because the compose adds no new GL entry point. The test files go from 27 to 28 |
| 2026-11-08 | same, milestone 6g | SSAO-STF (selective temporal filtering), camera-only scope (decision 31). The scene is static, so the history is validated by camera reprojection: the current pixel's world position is reconstructed from the current frame's scene depth (decision 20), projected with the previous frame's view-projection matrix, and the history is rejected when the reprojected position falls outside the screen, the reprojected position is behind the previous eye, or the history's depth disagrees with the current depth by more than the configured threshold (0.05 NDC). The history is a two-attachment ping-pong pair (attachment 0 = filtered occlusion, attachment 1 = this frame's NDC depth for the next frame's validation); the tonemap samples the filtered buffer when STF is on and the raw buffer when off. The filter is OFF by default: with it off, every pixel value is bit-identical to before the filter existed (the same rule the post chain follows). The per-frame camera exists (`setCamera`), which validates the camera and changes the NEXT frame without releasing the shadow maps — the prerequisite decision 18 recorded. The numeric half on OpenGL: camera move with a static scene keeps the corner occlusion stable within tolerance (206, 206, 206 across three frames), a disoccluded pixel gets raw occlusion (212 vs raw 206), and the first frame is the raw buffer. The structural half on the null backend: STF off by default (no STF pass, no history target), STF on creates the two-attachment ping-pong pair, `setCamera` does not release the shadow maps, and the config validation rejects blend outside [0,1) and depthRejection outside (0,1]. OpenGL off: 15/15 Debug; OpenGL on: 28/28 Debug and Release; `-L opengl` 16/16; docs_structure 23 checks, 0 failures; ssao 11 -> 15 checks, renderer 26 -> 41 checks; zero warnings under -Werror. No existing count decreased. The shader library goes from 26 to 27 entries and `ShaderProgramId` from 18 to 19 (PostSSAOStf); the contract stays at 37 pure virtual methods and gl_loader.cpp stays at 65 — no new GL entry point, because the STF pass renders through the same fullscreen-pass primitives the chain already uses. The STF pass is one more pass in the chain: the render contract gains no new method |
| 2026-11-08 | same, milestone 8 | The physics contract (8a), the null backend (8b) and the three real backends (8c): Bullet, Box2D 3 and PhysX 3.4 (decision 32). The contract is the classical layer only — rigid bodies, collision shapes, forces, gravity, joints, buoyancy and force fields, in SI units, Y-up — and carries no physics-API type: no Px*, bt* or b2* type leaves an adapter. The extra physics (PhysX 3.4's CPU cloth and soft body) is a backend capability, reported through capabilityFlags() (decision 32): PhysX reports cloth and soft body, Box2D reports 2D-only, the null backend and Bullet report none. The null backend is the exact reference: deterministic semi-implicit Euler over the world's fixed step. The real backends are measured against it: Bullet and PhysX reproduce the null's single-step scenarios bit-for-bit (fall velocity, fall position, force velocity, impulse, buoyancy), Box2D matches the velocities exactly and integrates positions inside its internal substeps, so its position tolerance is measured (0.219 m of difference against the null's 0.5 m for the force scenario). The adapters live in their own libraries behind the GRACE_ENABLE_PHYSX/BULLET/BOX2D options; the SDKs are external dependencies, cloned git-ignored, never committed. The PhysX adapter instantiates the default allocator and error callback, the default CPU dispatcher, and the null backend's protocol (shutdown, violations, double release). Suites: physics (null backend) 32 checks, physics_adapters 54 checks, 0 failures; the full default build 17/17 Debug; zero warnings under -Werror. All three adapter libraries build with zero warnings under -Werror |
| 2026-11-08 | same, milestone 9 | The script contract (9a), the null script backend (8b) and the AngelScript adapter (9b) (decision 33). The contract is decl-string based as data — registerType(name, size, flags), registerMethod/registerFunction(decl, native pointer, convention), registerGlobalProperty(decl, pointer) — and carries no scripting-language type: no as* type leaves an adapter. The null backend proves the registration surface and the violation protocol but cannot compile or run scripts (it has no language); the AngelScript adapter is the execution reference: a real script builds, functions are found by declaration, a native function registered through the contract is called from the script body, a registered global property is written from the script, a broken script fails build() with the SDK's message through lastError(), and a script exception fails execute() with the exception message. Suites: script (null backend) 41 checks, script_adapters 40 checks, 0 failures; the default build 17/17 Debug; zero warnings under -Werror. The adapter library builds behind GRACE_ENABLE_ANGELSCRIPT, so the default build stays SDK-free |
| 2026-11-08 | same, milestone 10 | The visual scripting contract (10a), the null backend (10a) and the graph compiler (10b) (decision 34). The contract is the vocabulary and the graph as data: a registry holds block descriptions (an execution block has at least one flow pin; an expression block has none), a graph references registry blocks by name and connects pins by name, and compile() is the only language-dependent operation. The null backend proves the registry and the graph validation; the graph compiler generates a deterministic AngelScript source for the graph, hands it to the script backend through the contract's addScriptSource/build/findFunction surface, and the entry point resolves by declaration and runs the flow chain on execute. The blocks are native functions registered into the script backend through the contract's registerFunction surface, so the visual layer registers through the same surface the application already uses (architecture 4.2). Suites: visual (null backend) 38 checks, visual_adapters 31 checks, 0 failures; the default build 18/18 Debug (SDK-free), the AngelScript build 20/20; zero warnings under -Werror. The adapter library builds behind GRACE_ENABLE_ANGELSCRIPT, so the default build stays SDK-free |
| 2026-11-09 | same, milestone 13 | The audio contract (13a), the deterministic null backend (13b) and the OpenAL Soft adapter (13c) (decision 37). The contract is the vocabulary and the backend is the mixer: buffers, voices (2D and 3D), per-voice reverb as a property, the listener and the streaming queue. The null backend is the deterministic reference: mix() advances every voice's cursor by the pitch rule, accumulates the mixed samples, and the 3D attenuation is a straight line from 1.0 at the reference distance to 0.0 at the maximum, so the same calls produce the same samples every run. The OpenAL Soft adapter opens the device lazily, queues the streaming chunks as AL buffers on the voice's source, and registers the reverb as a property (no EFX extension). The codecs are in the data layer, not in the backend calls: the WAV decoder is hand-written, the Ogg Vorbis decoder links libvorbisfile with callbacks over a memory span, and both turn file bytes into the PCM data the backend uploads. Suites: audio (null backend) 97 checks, audio_adapters 32 checks, 0 failures; the default build 21/21 Debug (SDK-free), the OpenAL build 22 tests (the adapter skips without a device, 77); zero warnings under -Werror |
| 2026-11-09 | same, milestone 11 | The motion module (decision 38): the data types (11a: skeletons, clips, tracks, events, blendshapes), the deterministic sampler (11b: the lerp on the positions, the slerp on the rotations, the forward kinematics, the blend, the events in (from, to] with the loop wrap) and the state-machine layer (11c: the trigger and exit-time transitions, the cross-fade from the previous state's pose, the look-at and foot placement controllers). The slerp entered the core math module, where the quaternion header's comment already promised it. Suites: motion 85 checks, 0 failures; the default build 22/22 Debug; zero warnings under -Werror |
| 2026-11-09 | same, milestone 14 | The GUI module (decision 39): the retained control tree with the hit test, the focus and the event dispatch carrying UTF-8 text (14a), the deterministic null backend with the fixed-metric font backend (14b) and the FreeType adapter behind GRACE_ENABLE_FREETYPE, measured against the system's DejaVu Sans face (14c). The UTF-8 decoder lives in the data layer (the two- to four-byte forms, the replacement code point on an invalid lead). Suites: gui 82 checks, gui_adapters 21 checks, 0 failures; the default build 23/23 Debug, the FreeType build 24 tests; zero warnings under -Werror |
| 2026-11-09 | same, milestone 15 | The game module (decision 40): the agent behaviours idle, patrol, seek and flee with the deterministic steering (the patrol walks its waypoints in order and wraps, the seek stops at the target, the flee moves away), the timers with the repeating fire and the timer events on the queue, the named properties whose kind is fixed by the first set, the event queue and the xorshift64* random source the application seeds explicitly (a zero seed and an unseeded draw throw). Suites: game 129 checks, 0 failures; the default build 24/24 Debug, the full-SDK build 28/28; zero warnings under -Werror |
| 2026-11-09 | same, milestone 16 | The video module (decision 41): the playback service as a deterministic frame stepper (the cursor advances by the delta seconds at the video's frame rate scaled by the playback's rate, the loop wraps, the playback stops at the last frame), the packed Yuv420 payload the application uploads, and the Ogg Theora decode layer behind GRACE_ENABLE_THEORA, measured against a real file (16 frames, 64x64, 16 fps). Suites: video 66 checks, video_adapters 11 checks, 0 failures; the default build 25/25 Debug, the Theora build 26/26, the full-SDK build 30/30; zero warnings under -Werror |
| 2026-11-09 | same, middleware submodules | The ten middleware SDKs became pinned git submodules (physx-3.4, bullet3, box2d, angelscript, sdl2, openal-soft, freetype, libogg, libvorbis, libtheora), each at a fixed upstream commit; `tools/add-submodules.sh` registers them idempotently and the CMake configuration builds each one on demand (ExternalProject, Release, outside the engine's warning set) into the build tree's `third-party/`. The PhysX checkout is patched in place by `tools/physx-patch.cmake` (the makefiles' `-Werror` replaced with the tw2-proven set, one header's const-correctness fix), the xiph chain is built in dependency order (ogg -> vorbis -> theora, theora's configure finds the ogg headers through the prefix's pkg-config files), and the physics_adapters test carries the PhysX runtime `LD_LIBRARY_PATH`. Suites: default build 25/25 Debug, OpenGL build 41/41, full-SDK build 28/28 (PhysX + Bullet + Box2D + AngelScript), OpenAL build 26/26, FreeType build 26/26, Theora build 26/26, AngelScript build 27/27; zero warnings under -Werror. The two-step flow (submodules + patch) validated on a fresh clone of the tree |
| 2026-11-09 | same, middleware submodules | SDL2 re-pinned to release-2.30.12 (the last revision of the 2.30.x series the engine supports; the engine's window, context, GL attribute and `SDL_GL_GetProcAddress` uses are unchanged across the series), and the shader-source rule test reads the submodule paths out of `.gitmodules` and skips them (measured: bullet3's examples and PhysX's KaplaDemo carry shading-language sources; without the skip the rule reported 8 hits in third-party code, 2905 files scanned, 0 hits with the skip). The four open questions of architecture §7 are resolved and recorded in "Open decisions" (3, 4, 5, 6). Suites: default build 25/25 Debug, OpenGL build 41/41 (SDL2 2.30.12 built from the new pin), docs_structure 23 checks, 0 failures; zero warnings under -Werror |
| 2026-11-09 | Ubuntu 26.04.1, x86_64, GCC 15.2.0, CMake 4.2.3, milestone 17 (decision 42) | Linux regression: the default build 25/25 Debug, the OpenGL build 41/41, zero warnings under -Werror |
| 2026-11-09 | same, MinGW-w64 GCC 13.2.0 cross-compile (x86_64-w64-mingw32), Wine 9.0 + Xvfb, milestone 17 (decision 42) | The tree cross-compiles clean under -Werror: 105 targets in the default build and 111 in the OpenGL build (SDL2 2.30.12 built from the submodule for Windows by the ExternalProject). Under Wine the default build 25/25 and the OpenGL build 38/38 (the two path-argument tests pass with the repo root as argv[1]); opengl_render runs 193 checks, 0 failures, and the full render pipeline (context, mesh, shader, lighting, shadows, post chain, readback) executes on Windows. Validation is Wine, not a native Windows run: the engine code has not been executed on native Windows hardware |

## Defects found by the tests, and fixed

| Defect | Cause | Resolution |
| --- | --- | --- |
| `glClear` raised `GL_INVALID_VALUE` and cleared nothing | Contract clear-buffer flags are `1`, `2`, `4`; GL expects `0x4000`, `0x100`, `0x400` | Explicit mapping to the GL mask |
| Off-centre readback returned undefined data | Readback happened after the buffer swap | Pre-swap `queueReadback()` and `lastReadback()` |
| OpenGL readback returned alpha 255 while the null backend returned the clear colour's alpha | The context was created without an alpha channel | Request `SDL_GL_ALPHA_SIZE` 8 |
| `describe()` reported "not running" after shutdown | Runtime state reported instead of historical fact | Version, vendor and renderer reported whenever a context was obtained |
| Mesh/shader tests drew with a uniform set before the program was active (`GL_INVALID_OPERATION`) | `glUniform*` requires a current program | Documented precondition in `shader.hpp` (setUniform requires `use()`); tests activate the program first |
| Published tree did not build with `GRACE_ENABLE_OPENGL=ON` | The lighting commit left the shader header unstaged and published half of a `const` change, so `drawIndexed() const` in the null backend had no matching override | Complete the change set and verify by building a fresh clone |

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
| `GRACE_ENABLE_PHYSX` | `OFF` | PhysX 3.4 physics backend (the submodule). |
| `GRACE_ENABLE_BULLET` | `OFF` | Bullet physics backend (the submodule). |
| `GRACE_ENABLE_BOX2D` | `OFF` | Box2D physics backend (the submodule). |
| `GRACE_ENABLE_ANGELSCRIPT` | `OFF` | AngelScript backend (the submodule). |
| `GRACE_ENABLE_OPENAL` | `OFF` | OpenAL Soft audio backend + Ogg/Vorbis. |
| `GRACE_ENABLE_FREETYPE` | `OFF` | FreeType GUI font adapter (the submodule). |
| `GRACE_ENABLE_THEORA` | `OFF` | Theora video decode adapter + Ogg/Vorbis. |

Middleware dependencies (pinned git submodules, built by the CMake
configuration when the corresponding option is enabled):

| Dependency | Pin | Version |
| --- | --- | --- |
| PhysX 3.4 | `5e42a5f1` (v3.4.0-rc-2-22) | 3.4.0 rc2 tree |
| Bullet 3 | `d3afe7613` (3.25-30) | 3.25 tree |
| Box2D | `dd8bd3fb` (v3.2.0-3) | 3.2.0 |
| AngelScript | `0020e24a` (v2.39.0~1) | 2.39.0 |
| SDL2 | `8236e01a` (release-2.30.12) | 2.30.12 |
| OpenAL Soft | `d3875f33` (1.23.1) | 1.23.1 |
| FreeType | `920c5502` (VER-2-13-2) | 2.13.2 |
| libogg | `e1774cd7` (v1.3.5) | 1.3.5 |
| libvorbis | `0657aee6` (v1.3.7) | 1.3.7 |
| libtheora | `7ffd8b2e` (v1.1.1) | 1.1.1 |

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

3. **Fidelity of the visual scripting block set.** The original block
    vocabulary is not publicly documented. A functionally equivalent set may be
    acceptable, but the decision affects the AngelScript type registration
    interface.
   **Resolved 2026-11-09: a functionally equivalent block vocabulary is
    accepted, and milestone 10 (decision 34) records it as the structure.** The
    documented material does not publish the original block set, so the
    contract's registry holds block descriptions and the graph references them
    by name; the vocabulary is an engine-owned set of native functions
    registered through the script backend's registerFunction surface, the same
    surface the application already uses. The graph compiler generates a
    deterministic AngelScript source for the graph, so the visual layer stays
    a layer and the engine never treats a block set as a specification.

4. **Save format and script state.** AngelScript has no built-in object
   serialisation. Game state should live in engine-owned structures wherever
   practical, with scripts holding behaviour rather than authoritative state.
   **Resolved 2026-11-09: the scene's state lives in engine-owned structures
   and the script state stays out of the save file (decision 36).** The
   serialisation (12b) writes the scene's entities, the partitioning result
   and the container's payload into the OWD container; the script state
   (the AngelScript globals and the visual scripting graphs) holds behaviour
   rather than authoritative state, and the save file does not serialise
   script objects. A game that wants to save script state must expose it
   through engine-owned structures, which is the same boundary the engine
   holds for the visual scripting graphs.

5. **Physics backend order.** The original was explicitly plugin-based over
   PhysX, Bullet and Box2D. Bullet is the natural first backend on licence and
   platform grounds; PhysX 3.4 is the second candidate if CPU cloth and soft
   body fidelity is required. PhysX 5 is not a candidate, because its
   deformable features depend on GPU execution.
   **Resolved 2026-11-09: Bullet is the first backend, PhysX 3.4 the second
   (decision 32).** The three adapters (8c) build behind their own options and
   the contract is the same across all of them; the physical backend a build
   links is the build's choice, not the engine's. The original's plugin
   contract is preserved as one abstraction, and the null backend is the
   deterministic reference both adapter suites are measured against.

6. **Engine name.** OpenGrace follows the convention of OpenMW, OpenRCT2,
    OpenTTD and OpenSAGE, all of which take their name from the original
    product. The closest precedent is OpenSAGE, which reimplements an internal
    EA engine.
   **Resolved 2026-11-09: the engine name stays OpenGrace.** The convention
   the four open-source reimplementation projects set is the one this project
   follows: the name carries the original product's name and states the
   project's intent. The milestone codenames (4b, 4c, the d-g increments)
   are the project's own numbering, not the original's, and the original
   engine's internal codenames are not reproduced.

7. **Shadow-pass contract shape: technique-named method or generic render-pass
    composition?** The 4b contract method `renderShadowMap(target, sun, bounds,
    submissions)` could not carry several cascades without becoming a different
    method. **Resolved 2026-11-06: option (b), generic render-pass
    composition.** The contract now exposes `beginRenderPass(target)` /
    `endRenderPass()` with `clear()`/`draw()`/`submit()` operating on the pass
    target, and no longer knows what a shadow is; `renderShadowMap` was removed
    and the cascaded pass is built from those primitives in the render module
    (`renderShadowCascades`), with the shadow map crossing `DrawSubmission` as
    plain data (`ShadowMap`). Reasoning: it serves a deferred shading path
    equally, and there was only one consumer to migrate. This was preferred
    over widening a technique-named method (option (a)), which would have kept
    the contract shadow-aware. **Validated by milestone 4d (2026-11-06): the
    dual paraboloid point-light shadow pass was assembled entirely from
    beginRenderPass/endRenderPass/ clear/draw plus plain data on DrawSubmission
    — no technique-specific method was added to the contract, and no contract
    method changed.** Decision 7 was correct: the second shadow technique
    needed zero contract changes.

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
    saying so. A silent fallback means a test can pass while the technique
    under test never runs, which is what happened in 4e. The fallback is
    currently documented but not observable; making it countable would have
    surfaced the situation in the first measurement instead of the second.

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

14. **Where do shader sources live?** **Resolved 2026-11-06 (milestone 5c):
    only in the OpenGL backend's shader library**
    (`src/render/opengl/shader_library.hpp`). The contract's shader factory
    takes a closed, API-neutral `ShaderProgramId` enum and the backend resolves
    it to its own source; callers (renderer, shadow passes, tests) never hold
    shading-language text. Rule for every future shader, stated once: a new
    shader = one `ShaderProgramId` entry + one entry in the backend library;
    the API-neutral render module must never contain shading-language source
    (enforced by the `shader_source_rule` test, which scans the tree and fails
    on any hit outside `src/render/opengl/`).

15. **Where is the linear-to-display boundary?** **Resolved 2026-11-06
    (milestone 6a).** Before 6a the pipeline wrote linear values straight into
    an 8-bit non-sRGB framebuffer — the output was missing a display encoding
    (0.5 linear measured as 128, where sRGB would give ~188). The boundary now
    sits at the end of the post chain: the HDR target (Rgba16F) holds linear,
    and the final 8-bit target receives sRGB(filmic tonemap(linear)). With the
    chain OFF every existing pixel value is bit-identical (legacy
    linear->8-bit, documented); with it ON the same scene measures 0.5 -> 206.
    Measured tonemap response (exposure 1): 0.0->0, 0.18->141, 0.5->206,
    1.0->232, 2.0->245, 4.0->252. Bloom halo measured at 4 px from a 4.0-bright
    source: 61 with bloom on, 0 with it off. The default remained the legacy
    path through 6a-6d so that no existing pixel value changed. **The flip
    happened in 6e (2026-11-06, decision 23): the chain is now the default**,
    and every measured value was re-baselined with before/after figures (see
    the 6e validation records): chain pixel = round(255 * sRGB(aces(x))) with
    aces(x) = x(2.51x+0.03)/(x(2.43x+0.59)+0.14) and srgb(c) =
    1.055*c^(1/2.4)-0.055 for c > 0.0031308; legacy was round(255*x). Example
    rows: 0.5 linear 128 -> 206, ambient 0.2 51 -> 149, clear colour 30 30 ->
    110, 6-light centre 253 -> 231. The legacy linear->8-bit path survives only
    as an explicit opt-out (CLI `--no-post`), documented as a compatibility
    path.

16. **Is presentation a device operation or a side effect of clearing?** It was
    a side effect. `clear()` read back queued pixels and swapped the buffer, so
    when the post chain cleared the default framebuffer part-way through itself
    the backend presented half a frame and captured the pixel before the
    tonemap ran. The command line showed it and no suite did: `clear()` in
    forward mode read 30,30,30,255 with the chain enabled and disabled alike,
    while the same scene through the renderer measured 206 for a 0.5 linear
    input. `swapBuffers()` now exists as a device capability, called once at
    the end of a frame by the renderer; `clear()` only clears. This is decision
    12 one level down: there the technique was hidden in a neutral name, here
    the frame lifecycle was hidden in a device operation. The null backend had
    mirrored the old behaviour and now mirrors the new one, which is what the
    shared contract checks caught.

17. **Is a constant ambient term acceptable, given that section 3.3 lists no
    ambient light type?** **Resolved 2026-11-06 (milestone 6b): yes, as a
    documented deviation, not as fidelity.** (a) The term is constant
    hemispherical indirect irradiance (a sky/ambient floor); a constant
    approximation is right here because no GI data exists yet.
    (b) It is modulated by albedo (reflected = albedo x incident).
    (c) It is scene-global (one scalar per frame, ~zero cost); per-object would
    need a G-buffer channel and is not justified before GI.
    (d) Light-volume-propagation indirect light ADDS to this term (ambient is
    the zero-frequency floor), so this work is not written twice; SSAO (6c)
    will modulate the ambient term by the occlusion factor — with this term in
    place, occlusion has something to occlude. Linear convention: same units
    as light intensity; default 0.05. Under the display transform the ambient
    does not disappear: the tonemap toe is near-linear and sRGB lifts low
    values (0.05 linear reads 13 in legacy 8-bit, ~59 through the post chain).
    Consequence fixed in 6b: occluded surfaces used to read 0,0,0,255 (a defect
    recorded since the render-target milestone); they now read the ambient
    contribution (13,13,13,255 legacy) in both paths.

18. **Where does the occlusion factor live, and how do the two paths get the
    same SSAO?** **Resolved 2026-11-06 (milestone 6c): option (b) — a second
    colour attachment on the HDR target carries the ambient contribution
    alone, and the tonemap composes `direct + ambient * occ`.** Decision 11
    (MRT, cap 4) already covers it: the HDR target uses 2 of 4 attachments, no
    contract change. Cost comparison recorded: (a) a forward depth+normal
    prepass would cost an extra geometry pass (the most expensive kind) plus a
    normal buffer and would make forward multi-pass; (b) costs one extra
    attachment write inside the existing lighting pass (free) plus the
    screen-space SSAO pass that every option needs; code-wise (b) fits the
    layering (the renderer composes passes; the contract stays generic).
    Normals (A1): the SSAO pass reconstructs view-space positions from depth
    and the normal from the depth gradient (screen-space cross product), so
    BOTH paths run identical SSAO math on identical scene depth — forward reads
    the HDR target's depth, deferred reads the G-buffer's depth (the HDR
    target's depth holds the lighting fullscreen quad there). Depth-gradient
    normals degrade at grazing angles (the gradient flattens); the 8-bit
    G-buffer normals would be adequate for this low-frequency kernel but are
    not used, to keep the paths byte-identical. Occlusion (A2): radius 0.5
    world units, 8 fixed hemisphere samples scaled 0.1..1.0 of the radius,
    bias 0.02 world units along the normal (the self-occlusion guard: a flat
    surface's samples sit bias above the plane and never register as occluded
    — measured: flat pixel 232 with SSAO on = 232 with it off). Output is an
    R8 occlusion buffer at full framebuffer size (~1 byte/pixel, 128x96 =
    12 KB; ~0.9 MB at 1280x720). The factor multiplies the ambient term ONLY:
    `aces((hdr - amb + amb*occ + bloom) * exposure)` then sRGB. Smallest
    resolvable change: occ is quantized in 1/8 steps (8 samples); at the
    default ambient 0.05 one step is ~2/255 in 8-bit output (marginal), so the
    tests use ambient 1.0 where one step is ~4/255 (measured 232 -> 228).
    Temporal filtering (A3): NOT implemented — SSAO-STF is deferred to its own
    increment; it would need per-pixel motion-vector reprojection, which
    nothing in the project supplies today. The prerequisite is larger than
    the shader: this tree has no motion to reproject. The camera is fixed
    from construction (`grep camera src/app/` finds nothing), the application
    submits nothing, no test moves the camera, and `setConfig()` releases the
    shadow maps so it cannot change only the camera per frame. A per-frame
    camera and a per-frame transform come first. What the non-temporal
    version does
    that a temporal one would not: it is deterministic (fixed sample pattern,
    no per-frame noise rotation), so tests are reproducible byte-for-byte.
    Resolved 2026-11-08 (milestone 6g, decision 31): the temporal filter is
    built, camera-only — the scene is static, so the history is validated by
    camera reprojection, and a per-frame camera exists (`setCamera`, which
    does not release the shadow maps). The 6g row records the scope.
    Path agreement (A4): forward and deferred read the same scene depth and
    run the same pass; measured near-corner pixel forward 206 = deferred 206
    (diff 0), open-area 232 = 232.

19. **Volumetric light types are documented and not implemented.** Section 3.3
    lists the supported light types as point, spot, directional and volumetric.
    The first three exist as `DirectionalLight`, `PointLight` and `SpotLight`
    with documented units and validation; nothing in `light.hpp` or the
    lighting shaders represents a volumetric light, and no shader in this tree
    scatters through a medium. This is a deviation from a documented capability
    and is recorded as one, per the rule section 3 now states.

    Deferring it was defensible on sequencing: a volumetric light is a
    scattering integral along a view ray, which needs shadowed space to march
    through. **Resolved 2026-11-06 (milestone 6f): implemented** as an analytic
    region (sphere for point-style, cone for spot-style) realized by a
    `volumeDensity` property on `PointLight`/`SpotLight` (default 0 = off, so
    no existing pixel changed). The documentation does not state whether
    "volumetric" is a distinct light type or a property of point/spot, nor
    which representation the original used — both recorded as deviations
    (decision 25). What was NOT implemented: directional (half-space) volumes,
    and shadowing of spot-style cone volumes, and of spot-light
    surfaces, which share the same cause (no spot shadow maps exist
    in this tree).

20. **Do fullscreen passes write depth, and where does scene depth live?**
    **Resolved 2026-11-06 (milestone 6d base repair): fullscreen passes never
    write depth, and the HDR target's depth attachment is the scene depth in
    BOTH paths.** Before the repair, `beginRenderPass` unconditionally enabled
    the depth test with writes, so the deferred lighting quad overwrote the
    HDR depth (0.5) after clearing it to 1.0 — the asymmetry decision 18
    worked around by reading G-buffer depth in deferred. The contract gains
    two GENERIC primitives (decision 7 shape, no technique names):
    `setDepthTestEnabled(bool)` (device state, default on; disabling
    GL_DEPTH_TEST also disables depth writes per the GL spec) and
    `copyDepthAttachment(src, dst)` (a blit of the depth attachment between
    same-sized targets; misuse is a counted violation). The renderer runs
    every fullscreen pass with the test off, and the deferred path copies
    G-buffer depth into the HDR target after its lighting pass, so SSAO (and
    DOF) read `m_hdr` depth unconditionally — the path-dependent branch is
    deleted. Proof (test_ssao): after a full frame the HDR depth readback is
    forward 0.9045 = deferred 0.9045 (the ground's scene depth), not 0.5.
    Existing occlusion measurements did NOT move (corner 206/232, flat 232,
    paths 206=206): the blit feeds the same depth the G-buffer read provided,
    so this is a DEFECT FIX (the asymmetry removed), not a re-baseline.

21. **What is the ambient default, given that 0.05 makes SSAO invisible in
    eight-bit output?** **Resolved 2026-11-06 (milestone 6d): 0.2 linear.**
    Measured candidates on the corner scene (probe, post on, ssao on,
    ambient-only) and in a lit scene (direct 0.5 + ambient*occ):
    A=0.05: ambient-only corner 59 -> 33 (occ 0.5), but in a lit scene one
    1/8 occlusion step moves the pixel 210 -> 210 (0 levels; the whole
    occ 1 -> 0.5 range is 2 levels) — invisible. A=0.10: corner 99 -> 59,
    lit step 1 level, lit range 8 levels, legacy occluded 26. A=0.15: corner
    128 -> 81, lit step 1 level, lit range 11 levels, legacy occluded 38.
    A=0.20 (chosen): corner 149 -> 99, lit step 1 level, lit range 14 levels
    (220 -> 206), legacy occluded 51 — shadows stay readable (20% grey).
    A=0.25: corner 165 -> 115, lit step 2 levels, legacy occluded 64 (25%
    grey — starting to wash shadows out). Consequences, all recorded as
    decision consequences rather than defect fixes: test_deferred occluded
    parity 13,13,13,255 -> 51,51,51,255 (both paths, diff 0); parity lit
    140 -> 178; test_post deferred+post 210 -> 220 (computed from config).
    CLI pixels unchanged (30 legacy / 110 through the chain — the centre
    sample is background).

22. **Where do DOF and morphological AA sit in the post chain, and in what
    domain?** **Resolved 2026-11-06 (milestone 6d).** Final order, one line:
    `lighting -> HDR(2 att + scene depth) -> SSAO(depth -> R8) -> MLAA(linear
    HDR) -> DOF(linear HDR, bokeh disc) -> bloom -> tonemap:
    sRGB(aces(direct + amb*occ + bloom)) [DISPLAY BOUNDARY] -> default 8-bit
    FB -> swapBuffers`. (a) DOF runs in linear HDR before the tonemap: a 4.0
    highlight next to 0.0 blurs to linear 2.0 -> aces(2.0) = 245; after the
    tonemap the same blur would average 252 and 0 -> 126. Linear is the
    physically correct domain (blurring light, not display codes). (b) DOF
    composes BEFORE bloom, so the bright pass thresholds the DOF-spread linear
    energy and an out-of-focus highlight blooms correctly. (c) MLAA runs on
    the LINEAR HDR, not the display-encoded output: the default framebuffer
    is not a sampleable target through the contract, the HDR target supplies
    exactly what edge detection needs (colour neighbours + scene depth), and
    linear thresholds are resolution- and gamma-stable. DOF specifics: CoC =
    clamp(aperture*|d-focus|/d / worldPerPixel, 0, maxCocPixels), d the view
    distance from the scene depth (camera near/far in world units supply the
    depth mapping); blur = 16 fixed disc taps, one gather pass, NOT separable
    (a separable blur is a Gaussian cross; a disc costs the full 2D tap count,
    bounded by the CoC clamp); silhouette measure: taps closer to the eye than
    the centre pixel are rejected, so a blurred foreground cannot bleed over
    a sharp background, while taps behind the centre are accepted (that is the
    bokeh spread); memory = one extra 2x Rgba16F full-res ping-pong target
    (~16.6 MB at 1280x720); no temporal or physically-based accumulation.
    MLAA specifics: a simple morphological pass in the shape of MLAA's edge
    detection — NOT SMAA (no pattern search, no reprojection); edges are
    Rec709 luminance contrast > 0.08 (linear) or NDC depth contrast > 0.02;
    one pass, 4-neighbour gather; a single-pixel edge blends to about half
    intensity, and a high-contrast interior boundary (bright HUD edge) is
    blended too — the stated cost of the simple morphology. Both techniques
    default OFF and leave every pixel unchanged when off (test_dof_mlaa:
    flat ambient-1.0 surface reads 232 with both off, matching the pre-6d
    tree).

    **Updated in 6e (decision 22 resolved): the chain now ends in an
    offscreen 8-bit display target, which IS sampleable through the contract,
    so MLAA runs AFTER the tonemap on the display-encoded image — the
    departure above is removed. The final order is now: lighting -> HDR ->
    SSAO -> DOF(linear HDR) -> bloom -> tonemap+sRGB [DISPLAY BOUNDARY] ->
    offscreen 8-bit display target -> MLAA(display-encoded) -> present
    (PostBlit fullscreen copy) -> default framebuffer -> swapBuffers. The
    MLAA luminance threshold now measures display-code contrast, uniform
    across exposure (on linear HDR the same threshold was generous in the
    dark and stingy in the light — edges in shadowed regions were missed).
    Cost: +2 Rgba8 full-res targets (~7.4 MB at 1280x720), +1-2 fullscreen
    passes, +1 shader (PostBlit); the contract needed no new method — present
    composes from beginRenderPass(nullptr) + bindTexture + draw.
23. **What does the engine do when nobody asks for anything (defaults)?**
    **Resolved 2026-11-06 (milestone 6e).** Default render path: forward
    (decision 1 unchanged — forward is the primary path). Default post state:
    **ON** (decision 15's flip; the chain is the product now that its
    techniques landed). Technique defaults inside the chain: SSAO ON
    (view-independent, darkens the ambient term only), MLAA ON
    (view-independent, part of the documented chain in section 3.2), bloom
    ON within the chain (threshold 1.0 linear, strength 1.0), **DOF OFF** — the
    focus distance is scene content, not a pipeline property, so a default
    focus would blur every scene unless it happened to match; enabled via
    config / CLI `--dof`. `grace` with no arguments renders through the chain
    and reports center_pixel 110,110,110,255 for the default clear colour 30:
    sRGB(aces(30/255))*255 = 110.51, GL conversion 110 (measured, both paths,
    visible and xvfb). The legacy linear path survives as `--no-post`,
    documented in the CLI help as a compatibility path (reports 30,30,30,255).

24. **Resource container: define one, or adopt an existing format?** The engine
    needs one: the documented feature set lists a resource manager among the
    core components. Two ways to get one, adopt an existing format or define
    one, and this project defines its own, the OWD format, specified in
    [resource- container.md](resource-container.md). Its payloads are DEFLATE
    streams (RFC
    1951) produced and consumed through zlib, which is a published IETF
    standard under the zlib licence and already a transitive dependency through
    SDL2, so the compression half of the problem is not invented here.

    The alternatives were rejected for stated reasons. The ZIP container
    (PKWARE APPNOTE) is published, well tooled and would have worked, but it is
    a general-purpose archive: it has nowhere to express what a resource is, so
    kind and dependency metadata would be smuggled into the comment field. Its
    central directory sits at the end of the file and is duplicated by per-
    entry local headers, so a random-access read seeks to the end and back and
    parses the same information twice. That is a cost of one extra seek and one
    redundant parse per read, not of heavy parsing, and it is stated precisely
    because the loose version of the argument is wrong. Loose files were
    rejected for having no index and no way to align an entry for a direct
    upload. The archive container of the game whose engine this project does
    not reimplement was rejected on provenance: its format is published
    nowhere, and copying a layout discovered from a shipped product would put a
    proprietary container at the centre of a project that reimplements a
    different engine generation.

    The format is specified by eight properties rather than by a byte layout,
    and the properties are the deliverable because they are what a caller
    depends on. Each one can be written without reference to any existing
    container, which is the test that separates a requirement from a copied
    structure. The encoding that satisfies them is in the specification
    document.

    - A stable identity per entry, so a name maps to exactly one resource.
    - A resource kind and declared dependencies, so a loader can answer what a
      resource needs before reading it, and can stream rather than load whole.
    - A per-entry method, stored or deflated, chosen by the build. A payload
      that is already compressed gains nothing from being deflated again and
      pays for the attempt in build time.
    - Alignment per entry, so a texture can be uploaded without an intermediate
      copy.
    - Random access without inflating the archive, so a level load reads only
      what it needs.
    - An index that permits appending, because a build adds resources rather
      than rewriting the container.
    - Overlay priority between containers, so a loose file or a later container
      overrides an earlier one without rewriting anything. Without this there
      is no modding story, and the alternative is the full directory rebuild
      this design exists to avoid.
    - An integrity check per entry, stored by the container rather than
      inherited from the compression. RFC 1951 defines no checksum, and the
      four-byte Adler-32 that makes zlib's RFC 1950 wrapper self-checking is
      absent from a bare Deflate stream, so the check belongs in the
      container's index and is not a free gift from zlib. This was stated the
      other way round in an earlier draft and the correction is recorded rather
      than quietly applied.

    The freedom from inherited constraints is part of the point. A container
    designed now answers to no console's 32-bit offset ceiling, no fixed-size
    name field and no alignment chosen for hardware that no longer exists.

    This is a deviation from the original in the same sense as the scalar math
    library, and it is recorded as one for the same reason: the original's
    choice is not available to be reproduced, so the honest position is to
    state what replaced it and why. It is not a shortcut and it is not a claim
    that the original's format was inadequate for its time. What is shared with
    the original is the problem it solved, not its solution: a game runtime
    needs a container with these properties, and that statement was true before
    any particular container existed.

    If interoperability with another container ever becomes a goal, the shape
    is the one this project already uses for physics: a neutral resource
    contract in the engine, and a format-specific backend outside the published
    tree, cloned locally. The engine's vocabulary stays its own either way.

25. **What is a light volume, and how is a volumetric light modelled?**
    **Resolved 2026-11-06 (milestone 6f).** The volume is an ANALYTIC region
    derived from the light's type and parameters: a sphere of `radius` around a
    point light, a cone (half-angle outerAngle, truncated by `range`) around a
    spot light. Not a generated mesh — this tree has no geometry-generation
    path, and a per-light mesh would need generation + upload per light per
    frame through a contract that only draws submitted meshes. Not a
    screen-space construct — a depth-buffer volume is view-dependent and cannot
    be shadowed along the ray; the analytic world-space test composes with the
    world-space lights, the single HDR scene-depth target (decision 20) and
    the shadow maps. Deviation: the documentation does not say which
    representation the original used, nor whether "volumetric" is a distinct
    type — it is implemented as a `volumeDensity` property on PointLight and
    SpotLight (default 0). Model (A2): single-scattering in-scattering
    integrated along the view ray — analytic ray-volume intersection, 16 fixed
    samples, per sample the documented attenuation x paraboloid shadow factor x
    ds, isotropic phase 1/(4pi); result added in LINEAR HDR inside the aces
    argument, so it responds to exposure. Camera inside the volume: the entry
    t0 is clamped to 0 (never marched backwards). The pass runs in the shared
    post chain on the HDR depth, so forward and deferred agree byte-for-byte
    (measured 162 = 162). Shadows along the ray (A3): point-style volumes with
    a paraboloid map test it once per sample (16 single-tap lookups per volume
    per pixel; the bilateral 3x3 filter stays a surface property). Spot-style
    cone volumes are unshadowed — known limitation (no spot shadow maps).
    The SURFACE of a spot light does not shadow either: both surface
    shaders gate the shadow lookup on `lightType == 0`, which is point
    only, so a spot light passes through geometry. Not a deviation —
    the documented shadow mechanisms are the paraboloid point map and the
    trapezoidal sun cascades, and no spot shadow is promised.
    Directional volumes not implemented (a half-space is not a useful shaft).
    Cap (A4): kMaxVolumetricLights = 4 per frame, over-cap rejected with
    std::invalid_argument in submit(), never truncated; the deferred surface
    cap of 16 is unchanged. Default OFF (A6): volumeDensity defaults to 0, so
    no pixel re-baselined; the CLI diagnostic frame has no lights, so `grace`
    reads 110,110,110,255 with and without the effect (application.cpp submits
    nothing).

Boundary checks that must keep passing:

```sh
grep -rn 'SDL\|GL/\|gl\.h\|GLuint' include/grace/core src/core || echo 'core is platform-free'
grep -rn 'GLuint\|GLenum\|SDL_\|Window' include/grace/render/render_backend.hpp | grep -v '^ *//' \
  || echo 'the render contract is API-neutral'
```
26. **How does the container compress payloads it cannot compress?**
    The specification's method field was a single bit, stored or deflated,
    which is not enough for the payloads this format exists to carry. Measured
    on payloads shaped like the ones it carries, an index buffer deflates to
    23.1% of its size and to 0.6% of it once its bytes are transposed first, a
    factor of 41, and the transposition is not compression: it is a
    rearrangement the container has to record or the payload cannot be read
    back. The method is now a filter plus a codec, the index record carries the
    filter and its element size, and the checksum covers the bytes after the
    filter is reversed, for the same reason it already covered the inflated
    bytes rather than the stored ones. The delta filter is measured to make
    things worse on the same data in a different order, 74.5% against 71.7%, so
    it is never chosen automatically and the build sets it per entry. A reader
    that implements only the unfiltered case reads any container that does not
    use a filter, so this is an extension rather than a break. The measurement
    is kept as tools/measure-filter.py, so the numbers in the specification can
    be reproduced rather than taken on trust.

27. **Is the index one deflated stream or a sequence of blocks?** One
    stream, and the answer is measured rather than argued. Requirement 5
    promises random access without inflating the archive; with a single index
    stream, finding one record inflates every record in the index, which was
    0.4 ms at 10,000 entries and 32.9 ms at 400,000, and grows with the
    archive. The index is now a block table followed by independently deflated
    blocks of 256 records: a lookup reads the table, 12 bytes per block, and
    one block, 12 microseconds at every size measured. The table is a flat
    0.5% of the index at every size, 18,756 bytes at 400,000 entries against
    the 3,513,508 it replaces. The header grew from 28 to 32 bytes to carry the
    block count. Measured by `tools/measure-owd.py`.

28. **Should the container carry a shared dictionary?** No, and the
    first measurement said yes until its inputs were inspected. A dictionary
    built from forty other meshes gained 9.7% on a 38 KB mesh and 29.3% on a
    small one; comparing the payloads byte for byte showed they differed in
    1.0% of their bytes, because the generator wrote a constant normal and a
    constant tangent into every vertex and only a noise pass read its seed, so
    the dictionary contained the payload it was measured against. Re-measured
    on payloads whose geometry actually differs, a dictionary makes them 1.8 to
    1.9% larger, gains 0.0% over the filter, and gains 3.2% in the one case
    that can be constructed to favour it, an LOD chain, where the 32,768-byte
    deflate window caps the gain at 4.0% of the payload. It would cost a header
    field, a build step, an invalidation rule and a cross-build compatibility
    constraint. Section 9 of the specification records its absence. Measured by
    `tools/measure-dict.py`.

29. **Should the payload be blocked, or is one deflate stream per entry
    enough?** Blocked, and the measurement is why. Requirement 2 says a loader
    can stream rather than load whole, and a single deflate stream is
    enterable only at byte zero: deflate carries its history forward, so a
    reader that wants bytes in the middle inflates everything before them.
    Measured on an 8.0 MiB vertex buffer, serving a 64 KB read in the middle
    inflates 8,400,000 bytes and takes 6.5 ms, against 131,072 bytes and 231
    microseconds with the payload blocked, 64 and 28 times less. On a 16 MB
    texture it is 14.4 ms against 223 microseconds. The payload now uses the
    shape the index already uses: a payload block table and independently
    deflated blocks, 256 KiB of output each, the size reduced to a multiple of
    the filter's element size. Two shapes were measured and one was rejected.
    Filtering the payload whole and blocking only the deflate keeps the
    filter's full effect, but the transposed stream is `element` planes, so a
    contiguous output range needs one run per plane and the read cost grows
    with the element size: 32 blocks against 2 on a 32-byte vertex record.
    Filtering per block costs compression, +21.55% on that vertex buffer and
    +91.60% on an index buffer at 256 KiB, and buys a read cost that does not
    depend on the element size. The penalty is 2.6% of the payload in bytes,
    against the filter removing 79% of it. No field was added to the record:
    the block size and the block count are both derivable from fields it
    already has, and an entry with one block has no table. Measured by
    `tools/measure-stream.py`.
30. **What blocks global illumination by light volume propagation?**
    Section 3.2 promises GI by light volume propagation and the milestone table
    lists it as milestone 7. "Planned" reads as "not started"; measured, it is
    blocked on a capability the render contract does not have, and the blocker
    is larger than the algorithm.

    Light volume propagation needs a 3D grid to hold and propagate irradiance.
    Measured on the published tree:

    - no 3D texture exists anywhere (`grep -rn -i 'TEXTURE_3D|Texture3D'` in
      `include/`, `src/` and `tests/` returns nothing);
    - there is no way to create a standalone texture at all. The
      `RenderBackend` contract has 31 pure virtual methods and none of them
      creates one: a texture exists only as an attachment of a render
      target, reached through `renderTargetTexture`;
    - `RenderTargetDesc` is 2D by construction: it has `width` and `height`
      and no depth field, so the existing attachment path cannot describe a
      volume;
    - the OpenGL backend cannot express one either. `gl_loader.cpp` loads 59
      functions and the only texture-image entry is `glTexImage2D`; the
      only framebuffer-attachment entry is `glFramebufferTexture2D`. The
      3D forms (`glTexImage3D`, `glTexSubImage3D`, `glFramebufferTexture3D`)
      are not loaded at all.

    The format is not the obstacle: `Rgba16F` is RGBA 16-bit float, ~8
    bytes per pixel, mapped to `GL_RGBA16F`, and the renderer already uses
    it as a renderable colour attachment for the HDR target. An earlier
    version of this entry listed the four `PixelFormat` values as unsuited
    to a float irradiance volume. That was wrong and is corrected here: the
    volume needs a third dimension, not a new format.

    So the first increment is not the propagation, it is the volume: a 3D
    texture in the contract (descriptor, creation, bind, in both backends and
    in the null backend), provable on its own by writing known values and
    reading them back. Rendering into the volume, feeding it from the scene,
    and propagating are the increments that follow, and the scope decisions the
    documentation does not settle — grid resolution, iteration count, whether
    geometry occludes, how many bounces — belong with them, measured the way
    decision 25 measured the volumetric light.

    Recorded now because a milestone marked "Planned" and a milestone blocked
    on a missing contract capability are different states, and only one of
    them can be started.

    Both counts above were wrong by one when this entry was first written and
    are corrected here. Measured with `grep -c '= 0;'` on
    `include/grace/render/render_backend.hpp` and
    `grep -c 'GRACE_GL_LOAD('` on `src/render/opengl/gl_loader.cpp`, each at
    the commit that published the numbers: 31 and 59, not 32 and 60. This is
    the same class of error as the `PixelFormat` claim corrected above, and it
    is recorded rather than silently replaced.

    A third number in the same commit was wrong the same way. The validation
    record for this increment claimed `-L opengl 14/14`, assuming the new
    `texture3d` suite would carry the `opengl` label. It does not: that label
    marks the suites that need an X server, and `test_texture3d` runs with no
    GL context at all, so the label still covers 13 suites. Measured from the
    run, `-L opengl` is 13/13 and the count is unchanged from the previous
    record. Written by presumption rather than measurement, in the commit that
    was correcting two numbers written by presumption.

    The first increment is built. `Texture3DDesc` (width, height, depth,
    `PixelFormat`) with `validateTexture3DDesc`; `createTexture3D`,
    `bindTexture3D`, `fillTexture3D` and `readTexture3D` in the contract, in
    the null backend and in the OpenGL backend; `glTexImage3D`,
    `glTexSubImage3D`, `glGetTexImage`, `glPixelStorei` and `glGetIntegerv`
    added to the loader. `TextureView` gained `depth()`, defaulting to 1 so
    every existing attachment view stays a single 2D layer, and the volume
    view overrides it. `bytesPerPixel(PixelFormat)` is declared once in the
    contract so a caller can size a slice without asking the backend.
    Two things were measured on the driver rather than assumed, and both
    changed the code. A depth/stencil volume is refused by the contract
    because the driver answers `glTexImage3D` with `GL_DEPTH24_STENCIL8` with
    `GL_INVALID_OPERATION` (0x0502). And row alignment 1 is set around every
    slice upload and read: with the default alignment of 4, an `R8` 3x3 slice
    does not round-trip, and with alignment 1 it does. Both were probed
    directly, and the second would have shipped as a silent defect.
    The round trip runs against both backends through one shared function,
    `runTexture3DChecks`, so the null backend proves it with no GL context at
    all (`test_texture3d`, 23 checks) and the OpenGL backend proves the same
    code path inside `test_opengl_render` (157 checks). A volume is still a
    resource and not a pass target: nothing renders into one yet, which is the
    second increment.

    The second increment closes that last sentence. A pass now renders into one
    layer of a volume: `createVolumeLayerTarget(TextureView*, layer)` returns a
    RenderTarget whose single colour attachment is that layer, attached with
    `glFramebufferTextureLayer`, so `beginRenderPass`, `endRenderPass` and
    `releaseRenderTarget` are the existing ones and no second pass protocol
    exists. A layer target borrows the volume and never deletes it, which is
    what makes a per-layer loop safe: releasing the target leaves the volume
    and its contents intact, and both backends are checked for that.

    Measured on this driver before the code was written, because the previous
    increment shipped a silent defect by assuming exactly this: attaching layer
    1 of a 4x4x3 volume is `GL_FRAMEBUFFER_COMPLETE` for Rgba8, Rgba16F and R8,
    with no GL error on create, attach, clear or read; clearing through the
    target changes layer 1 and leaves layers 0 and 2 untouched, which is the
    property the increment is about.

    The proof is on both sides. With no GL context at all, `test_texture3d`
    runs the same shared function, 23 -> 41 checks. With OpenGL, both backends
    run it inside `test_opengl_render`, 157 -> 193 checks: 36 more, which is 18
    per backend (15 checks plus one inside a three-layer loop). No existing
    count decreased. `gl_loader.cpp` loads 64 -> 65 functions, measured with
    the same `grep -c 'GRACE_GL_LOAD('` the earlier entry names: the previous
    increment took it from 59 to 64, and this one adds
    `glFramebufferTextureLayer`.
    The fourth increment makes the volume propagate: one step per
    iteration, each cell accumulating the average of its six face
    neighbours, each weighted by one sixth. The source volume is sampled
    with NEAREST at the cell centres so the taps are exact, and the
    CLAMP_TO_EDGE wrap makes an edge cell its own neighbour, so the
    volume's boundary cells keep their light instead of losing it to a
    neighbour outside the volume. The propagation step needs two volumes,
    because OpenGL forbids sampling a texture bound to the same unit as an
    active framebuffer attachment, so the renderer creates a second volume
    and the pointers swap at the end of every iteration: the frame's light
    volume is always the last one written.
    What the propagation step deliberately does not do: it does not
    occlude. The architecture promises GI by light volume propagation, but
    not which family; an LPV without occlusion is a light volume that
    leaks light through walls, and the plan's fourth increment names that
    decision with the scope it carries: resolution, iteration count,
    bounces. The geometry that would occlude is measured to exist in the
    G-buffer only, so the decision about it is recorded as open, not
    silently skipped.
    The third increment closes the sentence before that one: the scene now
    deposits its light into the volume. `VolumeConfig` (off by default) names a
    resolution and a world-space box; `runVolumeInjection` creates the volume,
    then runs one fullscreen pass per layer through the layer target the second
    increment added, so no new pass protocol exists for this either. The
    injection shader reuses the surface shader's attenuation exactly — the same
    `rangeAtten`, the same smoothstep cone, the same colours and intensities —
    so the volume cannot disagree with what the surfaces around it are lit by.
    What differs is only what is being lit: a point in space has no normal, so
    there is no N.L term and no specular.

    This increment also closed a gap the first one left. A volume could be
    created but never released before shutdown, so a renderer that rebuilt its
    volume on a configuration change could only leak it. `releaseTexture3D`
    was added to the contract for that reason, and it removes the view from
    the registry rather than flagging it the way `releaseRenderTarget` does,
    because a released volume must never be handed back: a layer target that
    outlived it would attach freed memory. The contract grew from
    36 to 37 pure virtual methods, measured the way decision 30
    declares: `grep -c '= 0;'` on the header. An earlier draft of this
    paragraph said 32 to 33. That was the same error decision 30 already
    corrected once, counting only the lines that carry both `virtual` and
    `= 0`, which silently drops every declaration long enough to wrap.

    The proof is a number, not a pixel, and it runs where the shader runs.
    `test_volume_inject` places one point light at (1, 0.5, 0.5) with intensity
    8 and radius 10, over a 4x4x4 volume spanning [0,4]^3, so the texel at the
    volume's lower-left corner has its centre at (0.5, 0.5, 0.5) and sits 0.5
    from the light. The value read back is 5.77344 against the 5.77600 the
    attenuation formula predicts: the difference is one half-float quantum,
    which is what an Rgba16F target can store. The third axis is checked the
    same way — layer 3's texel reads 0.37793 against 0.37793 predicted at
    sqrt(0.25 + 9) — so the pass is proved to write a different slice into each
    layer rather than repeating one. A bright light placed outside its own
    radius deposits exactly 0, which checks the windowed cutoff and not merely
    that the value is small.

    This test needs a GL context, and that is measured rather than assumed. A
    first version ran the same comparison on the null backend and measured
    0.00000 against an expected 5.77600: the null backend records events and
    never executes a shader, so it has no arithmetic to deposit a radiance
    value with. The numeric half therefore runs on OpenGL, and the structural
    half — off by default, release and recreate, a foreign view refused, a
    configuration rejected rather than clamped — runs on the null backend,
    where it needs no arithmetic at all. The suite gained test_volume_inject at
    26 checks and the test files go from 25 to 26. No existing count decreased.
    The shader library went from 24 to 25 entries and
    ShaderProgramId from 16 to 17 — the paragraph first said 17 to 18,
    which was one too high at both ends — and gl_loader.cpp stays at
    65 functions: this increment loads no new
    GL entry point, because the layer target it renders through was loaded by
    the second one.
    The fourth increment's second half composes the volume into the frame.
    `VolumeConfig` gained a `compose` flag (off by default), the submission
    carries the volume as plain data — a `LightVolume` struct with the texture,
    the origin, the extent and the resolution, the ShadowMap pattern, so the
    backend never reaches into the renderer — and the frame order inverts when
    compose is on: the volume passes run before the lighting pass, because the
    lighting pass samples the volume the volume passes filled. The compose
    sits in the lighting pass in both paths (decision 17(d)): the shader
    samples the volume at the fragment's world position and adds the sampled
    radiance to the ambient term.
    The first measured compose ran the forward path and left the deferred path
    dark, and the reason is a defect worth recording. The deferred shader
    declares `uniform sampler3D volumeTex`, but only the forward path set the
    sampler's unit, so with compose off the sampler defaulted to unit 0, which
    already carried a sampler2D. Two samplers of different types on one unit
    is GL_INVALID_OPERATION on the draw, and the entire draw is silently
    ignored, so every deferred lighting read came back with the clear colour
    and the compose looked like it did nothing. The fix sets the sampler's
    unit in both branches, on or off, and the same rule is now written in both
    paths: the sampler is set every frame.
    The same commit builds the cross-reference rule (R13): a source line
    naming a milestone and a decision must name a decision the milestone's
    row records. R6 checks that a decision number resolves, but nothing could
    check that the number names the right subject: twenty-four references in
    this repository cited a decision that exists but documents something
    else, and every one survived every rule and every review. The rule is
    narrow on purpose: it applies only where a line names a milestone as
    well, because that gives the check something independent to compare
    against — the milestone's own row states which decisions document it.
    Measured before the rule was written, 14 of the 71 decision references in
    the source name a milestone, and seven of those 14 were wrong; the other
    57 have nothing to compare against, so the rule leaves them alone rather
    than guess. A rule that reads source code is a scope change for the
    structural test, which read two documents before, and the fixtures could
    not catch it: the rule's fixture is a document plus a source line, and
    the fixture's own literal is built by concatenation so the test scanning
    itself cannot fail on its own text. The real tree passes with 23 checks
    and 0 failures on docs_structure, and an injected defect (a source line
    naming milestone 5 and decision 9) is caught by the rule.

31. **What does SSAO-STF validate its history against, and how much of the
    frame does it reproject?** Decision 18 (6c) deferred SSAO-STF because the
    tree had no motion to reproject: nothing in the project supplies per-frame
    motion vectors. The 6g row records the state that closes it: the camera is
    the only motion, and the camera is known, so the temporal filter is
    **camera-only** — a documented deviation from the documented capability
    list, which promises selective temporal filtering without naming the motion
    it reprojects.
    The history is validated against the current frame's camera reprojection:
    the current pixel's world position is reconstructed from the current
    frame's scene depth (decision 20: the HDR depth is the scene depth in both
    paths), projected with the previous frame's view-projection matrix, and
    the history is rejected when the reprojected position falls outside the
    screen or the history's depth disagrees with the current depth (the
    selective part: a disoccluded or off-screen pixel gets raw occlusion, not
    ghosted history). A pixel whose world position is unchanged between the
    frames (the camera did not move it) reads the previous frame's filtered
    occlusion; the blend is a linear mix between raw and history with the
    configured history weight.
    The history carries the occlusion factor in attachment 0 and the frame's
    NDC depth in attachment 1, so the validation is a texture lookup, not a
    second buffer: the depth comparison is a straight
    `abs(histDepth - d) > depthRejection`, and the history is written by the
    same pass that reads the previous frame's, so the two-attachment
    ping-pong target is the mechanism the decision records.
    The deviations are documented and measured: the camera is the only motion,
    so the world position is reconstructed by inverting the current frame's
    projection and view and reprojecting with the previous frame's
    view-projection; the scene is static, so the same scene point is the same
    world position in both frames. Geometry that moves would need motion
    vectors, which this tree does not have — the 6g row records the
    camera-only scope as a deviation, not an omission.
    The filter is OFF by default (the same rule the post chain follows: with
    it off, every pixel value is bit-identical to before it existed), and the
    cold start is the raw buffer (no history, so no history is read).
    The filter is validated by the numeric half of test_ssao: a camera move
    with a static scene keeps the corner occlusion stable within tolerance
    while the raw buffer would flicker, a disoccluded pixel gets raw
    occlusion, and the first frame is the raw buffer. The structural half
    (off by default, `setCamera` does not recreate the shadow maps) runs on
    the null backend.

32. **Which physics SDKs, and what is the contract's scope?** The documented
    physics layer was explicitly plugin-based, supporting PhysX, Bullet and
    Box2D behind one abstraction (architecture §3.6, §4.1). The contract is
    the classical layer only — rigid bodies, collision shapes, forces,
    gravity, joints, buoyancy and force fields, in SI units, Y-up, stepped
    from the engine's fixed-step clock — and carries no physics-API type: no
    Px*, bt* or b2* type leaves an adapter. That is the rule the original
    engine's plugin architecture implies, and it is what makes the null
    backend (8b) the exact reference implementation of the contract's
    semantics rather than a mock.
    PhysX is the "extra physics" the documented architecture describes: the
    PhysX 3.4-BSD release (the last BSD-licensed one; PhysX 5's cloth and
    soft body are GPU-exclusive) carries CPU cloth and soft body, which the
    classical layer does not cover. The contract therefore reports the extra
    layer through capabilityFlags() instead of pretending it is part of the
    classical layer: PhysX reports cloth and soft body, Box2D reports
    2D-only, the null backend and Bullet report none. The classical layer is
    identical across all four backends, and the real backends are measured
    against the null backend's deterministic integration (the milestone 8
    validation row records the agreement).
    The SDKs are external dependencies, cloned git-ignored, never committed:
    the repository stays source-only, and the NOTICE records the licences
    (BSD-3-Clause for PhysX 3.4, zlib for Bullet, MIT for Box2D). Havok is
    rejected on licence: it is proprietary, and the documented architecture
    is open-source-first.
    The three adapters are built behind the GRACE_ENABLE_PHYSX, BULLET and
    BOX2D options, so the default build stays free of every SDK. The
    protocol (shutdown, violation counts, double release) follows the null
    backend's, and each adapter is measured for the same scenario battery
    the null suite defines.

33. **What is the script host, and what does the contract carry?** The
    documented architecture gives the engine a script host: text scripts
    compile to bytecode the engine owns, the engine registers application
    types into the script engine, and the visual scripting layer (milestone
    10) compiles its graph to a callable entry point (architecture §4.2).
    The contract (9a) is decl-string based as data — registerType(name,
    size, flags), registerMethod/registerFunction(decl, native pointer,
    convention), registerGlobalProperty(decl, pointer) — and carries no
    scripting-language type: no as* type leaves an adapter, the same rule
    decision 32 set for the physics SDKs. The null backend (8b) is the
    protocol reference: it proves the registration surface and the
    violation protocol but cannot compile or run scripts (it has no
    language). The AngelScript adapter (9b) is the execution reference: it
    maps the contract onto asIScriptEngine — PlainValue maps to
    asOBJ_VALUE | asOBJ_POD, the contract's CdeclObjFirst/Last map to
    asCALL_CDECL_OBJFIRST/OBJLAST, and build errors are captured through
    the SDK's message callback into lastError(). Registration is
    engine-wide in the SDK, so the backend keeps its own lists and applies
    them into every engine created through it. The SDK is cloned
    git-ignored and linked when GRACE_ENABLE_ANGELSCRIPT is enabled, so
    the default build stays free of it; the licence entry is in NOTICE.
34. **What is the visual scripting layer, and what does its contract carry?**
    The documented architecture describes the visual scripting system as a
    graph representation whose blocks are types registered into the
    AngelScript engine; each graph compiles to a callable entry point
    (architecture §3.5, §4.2). The contract is the vocabulary and the graph
    as data: a registry holds block descriptions (an execution block has at
    least one flow pin; an expression block has none), a graph references
    registry blocks by name and connects pins by name, and compile() is the
    only language-dependent operation. The documented material does not
    publish the block vocabulary, so the contract records the structure and
    defers the vocabulary to the adapter; the null backend is the protocol
    reference (registry, graph validation, compile() fails with a
    deterministic lastError(), violations counted), and the real adapter
    The graph compiler (milestone 10b) maps the vocabulary onto the script
    contract's registration surface: a block's native function is registered
    into the script backend through the contract's registerFunction surface,
    with the declaration "void BlockName()", and compile() generates a
    deterministic AngelScript source for the graph, hands it to the script
    backend as a module named after the graph, builds it and resolves the
    entry point by declaration. The application owns the script backend and
    passes it in with setScriptBackend(); the visual layer registers its
    blocks through the same surface the application already uses
    (architecture §4.2).
35. **Is the container a specification or an implementation?** Both, and the
    order matters. The specification (decisions 24 and 26 to 29) came first
    and the implementation follows it file for file: `src/owd/owd.cpp` is the
    writer and the reader, `src/owd/internal.cpp` the CRC-32 and Deflate
    codec, `include/grace/owd/types.hpp` the contract's types, and
    `src/owd/owdtool.cpp` the `owdtool` command line that packs a directory,
    unpacks a container and lists one. The suite is 38 checks: deterministic
    output (two runs byte-identical), a whole-payload round trip, a
    multi-entry index in canonical order, both filters' round trips,
    `readRange` over a blocked payload, a corrupted byte rejected by the
    per-entry checksum, the overlay resolution and the writer's refusals.
    Two defects surfaced and were fixed before the suite went green, and both
    are worth recording. The writer serialised its index records twice, the
    second pass declaring a 36-byte fixed part where the format's is 38, so
    the buffer under-ran by two bytes per record; and the reader derived the
    index block table's end by scanning for a zero offset, a value no writer
    emits, instead of using the header's block count. The first crashed the
    writer, the second made the index unreadable; the suite covers both now.
    Measured: `owdtool` packs the same directory twice to byte-identical
    files (SHA-256 verified), `ctest` runs 19/19 with the container suite
    among them.

36. **Where does the scene's state live, and what is its serialisation
    strategy?** The scene is engine-owned plain data, and the serialisation
    strategy (the open question decision 33's note recorded) follows the
    answer the note itself recorded: game state lives in engine-owned
    structures wherever practical, with scripts holding behaviour rather
    than authoritative state. The scene (12a) is the entity list: a name, a
    position, a scale, a mesh index, a material index and a list of LOD
    steps. The partitioning is selectable among flat, grid, octree and
    quadtree (architecture §3.8), and the same entities and the same query
    produce the same result under every choice, in the same order — the
    partitioning is a query strategy, not a data reorganisation. The
    serialisation (12b) writes the scene's state into the OWD container as
    one entry: the engine-owned format carries the entities, the container
    carries the bytes, the checksums and the overlay. The scene's payload
    is deterministic: the same entities produce byte-identical bytes, so
    the container's own checksum requirement is met by the scene's data
    alone. The suite covers the partitioning selection, the queries, the
    LOD, the refusals and the serialisation through the container.
    Measured: `scene` 129 checks, 0 failures; the default build 20/20.

37. **How does the audio backend mix sound, and where does the codec live?**
    The contract is the vocabulary and the backend is the mixer: buffers,
    voices (2D and 3D), per-voice reverb as a property, the listener and the
    streaming queue. The null backend is the deterministic reference: `mix()`
    advances every voice's cursor by the pitch rule, accumulates the mixed
    samples, and the 3D attenuation is a straight line from 1.0 at the
    reference distance to 0.0 at the maximum, so the same calls produce the
    same samples every run. The OpenAL Soft adapter is the execution
    reference: the device opens lazily, the streaming chunks are queued as
    AL buffers on the voice's source, and the reverb is registered as a
    property (no EFX extension). The codecs are in the data layer, not in
    the backend calls: the WAV decoder is hand-written (RIFF, `fmt`, `data`),
    the Ogg Vorbis decoder links libvorbisfile with callbacks over a memory
    span, and both turn file bytes into the PCM data the backend uploads.
    The null mixer measures 97 checks against the contract; the adapter, 32
    checks against the same contract with the null driver. Suites: audio
    (null backend) 97 checks, audio_adapters 32 checks, 0 failures.

38. **Is the motion module a device contract with backends, or plain
    deterministic data?** Plain deterministic data, the scene module's
    pattern. The documented architecture's animation is CPU evaluation
    (skeletal keyframes, state machines, blending, node controllers,
    events, blendshapes) with the skinning hardware-side, so there is
    no device to abstract: the sampler, the machine and the
    controllers are a library the render and application layers
    consume. The same clip and the same time produce the same pose
    every run, which is what a deterministic test can check. The
    slerp, which the quaternion header's comment already promised,
    landed in the core math module as the rotation interpolation the
    key tracks and the cross-fade share.

39. **Is the GUI a device contract or a service, and where does the
    font rasterisation live?** A service with one backend surface for
    the fonts and none for the tree, the scene module's pattern once
    again: the retained controls, the hit test, the focus and the
    event dispatch are plain deterministic data, so the null backend
    is the protocol reference and no device abstraction exists for
    the tree. The fonts are the one real-world boundary (the
    documented architecture names FreeType), so the font backend is
    a contract of its own: the null implementation's fixed metrics
    are the deterministic reference, the FreeType adapter behind
    `GRACE_ENABLE_FREETYPE` is the execution reference, measured
    against the system's DejaVu Sans. Text is UTF-8 through the
    whole contract; the decoder lives in the data layer, not in the
    backend calls.

40. **What do "agent behaviour" and "gameplay-level services"
    cover, and where does the randomness come from?** The module
    table names the AI module's scope in one phrase, agent
    behaviour, and the Game module's in another, gameplay-level
    services; the documented material says nothing more specific
    about either. This project reads both as deterministic engine
    services, the scene module's pattern: the behaviours the built-in
    set covers are idle, patrol, seek and flee, driven by
    application-supplied targets and waypoints, and the services are
    timers, named properties, an event queue and a random source.
    The random source is a xorshift64* generator the application
    seeds explicitly, so a scripted sequence is reproducible by
    construction and no library random state exists. Nothing in the
    module names a navigation mesh, a path finder or a scheduler:
    the documented material names none, and the module stays the
    deterministic core the higher layers build on.

41. **What does the "Playback service" cover, and which codec
    carries it?** The module table names the Video module's scope
    in one phrase, playback service, and the documented capability
    list says nothing about video at all: section 3 has no video
    entry, and the vendor's own material (the engine ran Bink,
    a proprietary codec) names none this project can use. This
    project reads the module as the scene module's pattern once
    again: a playback is a deterministic frame stepper over a
    decoded frame payload the application uploads, and the codec
    question lives entirely in the data layer. The open codec is
    Ogg Theora, the same ecosystem as the audio module's OpenAL
    Soft and Ogg Vorbis; the decode layer behind
    `GRACE_ENABLE_THEORA` is the execution reference, measured
    against a real file, and the backend never sees a codec. The
    packed payload is planar YCbCr, the format the decoder
    produces; the RGB conversion is the renderer's sampling
    concern, not a backend call.

42. **How does the OpenGL backend release the video subsystem when a
    backend instance is torn down?** The engine creates the video
    subsystem with `SDL_Init(SDL_INIT_VIDEO)` per backend instance
    and the documented SDL2 behaviour is that `SDL_Quit` is not
    reference counted: it tears down the video subsystem for every
    backend instance in the process, so a sibling backend's
    `SDL_GL_MakeCurrent` and `SDL_GL_GetProcAddress` fail after it.
    **Resolved 2026-11-09 (milestone 17): the teardown releases the
    caller's own reference with `SDL_QuitSubSystem(SDL_INIT_VIDEO)`,
    which is reference counted, and unbinds the context with
    `SDL_GL_MakeCurrent(nullptr, nullptr)` before deleting it.**
    The measured Windows behaviour (see the validation records,
    milestone 17) is that SDL keeps the most recent context current
    for the whole process, so a backend that leaves its context
    bound on delete would leave a sibling backend's GL calls running
    without a current context; `drainGlErrors` loops forever on
    `GL_INVALID_OPERATION` in that state. The rebind before every GL
    entry point (`makeCurrent`) is the defensive half of the same
    decision: the backend no longer assumes the process-wide context
    belongs to it.

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
