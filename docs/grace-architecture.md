# OpenGrace — Architectural Reference

Working specification for an independent, source-only reimplementation of the
GRACE 2 engine architecture, targeting native Linux x86_64 with OpenGL.
Apache-2.0 licensed.

This document is compiled **exclusively from publicly available sources**. It
contains no proprietary material, no extracted data, and no content derived from
non-public software. See "Sources" at the end for the full provenance list.

---

## 1. What GRACE 2 was

GRACE 2 was the in-house game engine of Reality Pump Studios, a division of
TopWare Interactive, based in Kraków, Poland. It succeeded the original GRACE
engine. *Two Worlds II* (2010) ran on that original GRACE, and the official
site for that game credits "the all-new GRACE game engine", which is the
clearest statement of the relationship that exists. The GRACE2 site draws the
same distinction in its own words, though less precisely: "its previous
versions have powered ... games from the Two Worlds franchise", which the
franchise's later titles and expansions satisfy without naming the first game.

The engine behind the original *Two Worlds* (2007) is not publicly documented.
Community discussion sometimes calls it "Antalor", after the name of the
game's world, but no official source uses that term and this document does not
adopt it. What can be said is the negative: it is not described anywhere as a
GRACE version, and the assumption that it was belongs to the same class of
inherited belief that this section has already had to correct.

Which titles ran on the engine is harder to establish than the architecture.
The official site displayed "example screenshots from games that have been
created on GRACE2", and that gallery showed *Cluster Frenzy*, *4Hunters
Genesis*, *3switchD* and a port of *Chicken Shoot* for the iPad 2, alongside
screenshots from *Sacrilegium*. That gallery is the strongest public evidence
available, and it is thin: promotional screenshots rather than a credits list.
*Raven's Cry* (2015) is often associated with this engine, and this document
does not count it as evidence. The development history makes the association
understandable: the project began at Octane Games, failed to meet its
timetable, and was finished by Reality Pump from 2013. But the only
encyclopedic source that names its engine names Havok Vision, a Trinigy
middleware product acquired by Havok in 2011 and unrelated to GRACE 2. A
conflict between an association and the one source that states a fact is not
evidence for either, so it is excluded here. Beyond these titles the public
record is incomplete, and this document does not attempt an exhaustive list.

The architecture recorded here is the engine as the public material describes
it between 2012 and the studio's closure in 2015, which is its final stage. It
is not the engine behind *Two Worlds II*, and no system in this document should
be read as a description of that game's technology.

The studio ceased development in 2015. No source release ever occurred, and the
engine is not available in any form today. The GRACE 2 SDK was distributed as a
public beta between 2012 and roughly 2014 under a free non-commercial licence;
that distribution channel is gone.

This project reimplements the architecture described in public documentation. It
is not affiliated with Reality Pump or TopWare Interactive.

---

## 2. Engine modules

The public feature documentation organises the engine into these functional
areas. The distributed SDK's module layout corroborates the same decomposition:

| Module | Responsibility |
| --- | --- |
| Core | Object model, resource manager, threading, math, curves, profiler |
| Std | Standard-library layer, containers, strings, localisation |
| Graphics | Renderer, scene graph, materials, shadows, post-processing |
| GraphicsShaders | Shader sources and variants |
| Physics | Plugin-based physics abstraction and backends |
| Script | AngelScript host and the visual scripting system |
| Audio | 2D/3D playback, streaming, backend abstraction |
| Video | Playback service |
| Motion | Animation, state machines, blending |
| AI | Agent behaviour |
| GUI | Retained UI system, fonts, unicode input |
| Game | Gameplay-level services |
| App | Application lifecycle, platform entry points |

---

## 3. Documented subsystem capabilities

The following is the published capability set of GRACE 2, and it is what this
project reconstructs against.

It is a specification, not a verified feature list. The product was withdrawn
while still described as a beta: the official site titled itself "GRACE2
beta", the SDK was published as version 0.4.1, and the titles the vendor
exhibited were small games and ports. The project that would have anchored the
engine, *Sacrilegium*, was cancelled, and the studio closed in 2015. No shipped
game demonstrates most of what follows, and some entries may have been planned
rather than present.

The distinction changes how these entries are read. An entry here is a claim a
vendor made about a product it was still building, and the honest response is
to implement it and say that is what is happening. Treating an entry's presence
as evidence that the original behaved that way is what this document tries not
to do, and where this project departs from an entry, the departure is recorded
as a decision rather than folded in as if it were fidelity.

### 3.1 Core and general

- Portable, highly optimised C++ with an object-oriented, responsibility-driven
  design.
- Task-based multithreading.
- Resource manager.
- SSE math library.
- Spline, B-spline and TCB curve support.
- Built-in profiler and runtime data validator.
- Localisation with UTF-8 and UTF-16.
- Compressed texture support: PVR, ETC, ATC, DXT.
- Stereoscopic 3D output.

### 3.2 Graphics

- Advanced rendering system with flexible shaders.
- Target APIs: Direct3D 9, Direct3D 10/11, and **OpenGL 3.0+**.
- HDR rendering.
- Bloom.
- Filmic curve tonemapping.
- Depth of field with bokeh.
- Morphological anti-aliasing.
- SSAO with selective temporal filtering (SSAO-STF).
- Global illumination by **light volume propagation**.

### 3.3 Lighting

- Supported light types: point, spot, directional, **volumetric**.
- Forward rendering with dynamic lights, up to four lights per object plus
  sunlight.
- Deferred rendering supporting an unlimited number of lights.

> Note (implementation, milestone 6f): the source does not say whether
> "volumetric" is a distinct light type or a property of point/spot lights,
> nor which representation (analytic region, mesh, screen-space) the volume
> has. This project implements it as a scattering-density property of point
> and spot lights with an analytic volume; see decision 25 in
> [implementation-status.md](implementation-status.md).

### 3.4 Shadows

- Dual paraboloid shadow maps with bilateral filtering.
- Dynamic sunlight shadows using trapezoidal cascaded shadow mapping with
  bilateral filtering.

### 3.5 Scripting

- **AngelScript** for text-based scripting.
- **Tetragramaton** for visual scripting, referred to in the engine code as VAS
  (Visual AngelScript). Visual blocks are types registered into the AngelScript
  engine; each graph has a single entry point treated as a normal AngelScript
  function and callable from script by name.
- Both scripting modes ship with an editor and a debugger.
- Scripting operates at three levels: network, application, and scene.

### 3.6 Physics

- **Plugin-based physics architecture**, supporting PhysX, Bullet, and Box2D as
  interchangeable backends.
- Rigid body simulation.
- Buoyancy forces.
- Joints.
- Customisable force fields.

### 3.7 Animation

- Hardware-skinned, compressed skeletal keyframe animation.
- Bone budgets by platform: 32 bones per object on mobile, 8192 on high-end.
- State-machine driven animation with blending across up to eight layers.
- Custom node controllers, including look-at and foot placement.
- Animation events: hotspots and actions.
- Blendshapes for morphing.

### 3.8 Scene management

- Data-driven entities.
- Spatial partitioning selectable among flat, grid, octree, and quadtree.
- Scene queries.
- Level of detail.

### 3.9 User interface

- Built-in UI system with standard controls.
- Font rendering via FreeType.
- Unicode text rendering and user input.
- Support for 3D GUI systems.

### 3.10 Sound

- 2D and 3D audio playback and streaming.
- Supported formats: Ogg Vorbis and WAV.
- Per-sound reverb.
- Backends: OpenAL on PC, Android, macOS and iOS; OpenSL on Android; XAudio2 on
  PC and Xbox; MultiStream on PlayStation 3.

### 3.11 Tools

- More than a dozen editors covering the full content pipeline.
- Network-based tools optimised for LAN and internet workflows.

---

## 4. Target mapping for OpenGrace

OpenGrace implements Linux x86_64 with OpenGL, and records Windows and Android
as planned targets. This is a subset of the original platform matrix, and the
implemented part is well supported: OpenGL 3.0+ was a documented GRACE 2
target.

| GRACE 2 area | OpenGrace approach |
| --- | --- |
| Direct3D 9/10/11 | Out of scope. OpenGL and OpenGL ES only. |
| OpenGL 3.0+ | Primary target. Core profile, GLSL 3.30 or newer. |
| Console platforms | Out of scope. |
| Windows | Planned, not started. OpenGL, same backend; see 4.3. |
| macOS | Not a target. Apple deprecated OpenGL in macOS 10.14, so a backend would be possible but would have no future; see 4.3. |
| Mobile platforms | Android is a planned target, not implemented. OpenGL ES 3.0 would be the API; see 4.3. |
| PhysX / Bullet / Box2D | Plugin contract preserved; backends chosen by licence fit. |
| OpenAL / OpenSL / XAudio2 / MultiStream | Plugin contract preserved; OpenAL Soft on Linux. |
| AngelScript + Tetragramaton | Both reimplemented as engine services. |
| Resource manager | Container of this project's own over DEFLATE payloads; see 4.4. |
| Editors and network tools | Deferred; out of scope for the first milestones. |
| Stereoscopic 3D | Deferred. |

### 4.1 Physics decision

GRACE 2's physics layer was explicitly plugin-based, supporting PhysX, Bullet,
and Box2D behind one abstraction. OpenGrace reproduces that abstraction rather
than choosing a single engine.

Backend candidates, by licence fit:

| Backend | Licence | Notes |
| --- | --- | --- |
| Bullet | zlib | Permissive, no royalty, CPU-first, small surface area |
| PhysX 3.4 | BSD-3-Clause | Permissive; includes CPU cloth and soft body |
| Box2D | MIT | 2D only |
| PhysX 5 | BSD-3-Clause | Cloth, soft body and particles are GPU-exclusive |

Given the Linux CPU-only target, Bullet is the natural first backend, with
PhysX 3.4 as a second if cloth and soft body fidelity is required. PhysX 5 is
not a candidate: its deformable features depend on GPU execution.
The implementation follows this table: Bullet, Box2D 3 and PhysX 3.4 are the
three real backends behind the contract (milestone 8, decision 32), and
PhysX 3.4 is the extra-physics capability the engine describes — CPU cloth and
soft body, reported through the contract's capability flags rather than
folded into the classical layer. The classical layer is identical across
backends, and the null backend is its deterministic reference implementation.
The abstraction boundary matters more than the first backend. No `bt*`, `Px*`,
or `b2*` type may appear outside its adapter.

### 4.2 Scripting decision

Both scripting systems are in scope, and the relationship between them is the
important part: Tetragramaton is not a separate interpreter, it is a graph
representation whose blocks are AngelScript types. A graph compiles to a
callable AngelScript entry point.

This means the AngelScript host must support type registration from the visual
layer from the start. A host that only runs text scripts would have to be
rebuilt to accommodate VAS later.

Serialisation is an engine-owned concern. AngelScript exposes object member
iteration and a module-global serialiser, but provides no automatic save format.
Game state should live in engine-owned structures wherever practical, with the
script layer holding behaviour rather than authoritative state.

---

### 4.3 Platform targets

| Platform | Graphics API | Status |
| --- | --- | --- |
| Linux x86_64 | OpenGL 3.3 core, falling back to 3.0 | Implemented and validated |
| Windows x86_64 | OpenGL | Planned, not started |
| Android | OpenGL ES 3.0 | Planned, not started |

Only Linux is implemented. Windows and Android are recorded so the intended
scope is explicit rather than implied, and nothing in the repository supports
either today.

Windows is expected to be a small change rather than a port. The window and the
context come from SDL2, the render contract is API-neutral, and the OpenGL entry
points are resolved at runtime through `SDL_GL_GetProcAddress` rather than being
linked directly, so there is no import library to replace. The build has not
been exercised on Windows and the loader has not been reviewed for it.

Android is a larger change and is not primarily a build-system question. OpenGL
ES is a different profile with a different shading language: the shaders target
GLSL 3.30 core and would need a GLSL ES 3.00 path, and that difference would
have to be absorbed inside the OpenGL backend so the render contract stays
API-neutral. The application lifecycle also differs, because the application
does not own its window and the context can be lost and recreated.

macOS is not a target. The published material lists it among supported
platforms, so this is a scope decision and not a claim about the original.
Apple deprecated OpenGL in macOS 10.14 rather than removing it, so a backend
remains possible; it would be written against an API the platform vendor has
abandoned, and that is the reason it is out of scope rather than a technical
impossibility. The distinction matters: deprecated is not the same as absent,
and this project does not get to call something impossible because it is
unattractive.

Android carries one concrete requirement that belongs here rather than in a
future porting note. The HDR target is a 16-bit float colour attachment, which
OpenGL ES 3.0 does not require to be renderable: it needs
`EXT_color_buffer_float` or `EXT_color_buffer_half_float`, or OpenGL ES 3.2.
An Android backend would therefore depend on an extension the target table
does not currently mention, and discovering that during a port would be an
expensive way to learn it.

The physics and scripting backends are not wired yet, so neither platform
introduces a dependency that is currently unavailable there. The PhysX SDK 3.4
upstream lists Windows, Linux, OSX, Android and iOS among its supported
platforms, which is part of why Windows and Android are the two recorded here.

### 4.4 Resource container

The documented feature set lists a resource manager among the core components,
so this project needs a container format. The original did not expose
containers through its plugin architecture and its own format was never
published, so there is nothing documented to reproduce: the container is this
project's own, the OWD format, specified in [resource-container.md](resource-container.md)
rather than inherited from any byte layout.

Its payloads are DEFLATE streams (RFC 1951) produced and consumed through zlib,
a published IETF standard under the zlib licence and already a transitive
dependency through SDL2. The format is defined by eight properties, each
derivable from what a runtime needs, and the encoding of those properties is
the specification. Nothing in it names a graphics API or a host platform.

### 4.5 Future rendering backends

OpenGL is the only backend implemented, on Linux x86_64. The render contract is
API-neutral by construction and the boundary is checked by grep in every
milestone, so a second backend is a new implementation of the same contract
rather than a rewrite. Two are planned and neither is started.

**Vulkan** would cover Windows, Linux and Android with one backend. **Metal**
would cover macOS and iOS with one backend, and on iOS it is the only option
left: Apple removed OpenGL ES, while on macOS OpenGL is deprecated but still
present. Both are recorded here as intent, not as work in progress.

The contract was designed against OpenGL, and the second backend is what will
test it. The parts expected to survive unchanged are the ones about resources
and passes: mesh and texture creation, `beginRenderPass` and `endRenderPass`,
which map almost directly onto Vulkan render passes and Metal render encoders,
and `swapBuffers`, which maps onto swapchain presentation. The parts expected
to need work are the ones that are about immediate state.

`setDepthTestEnabled` is a global toggle in OpenGL and a pipeline state in
Vulkan and Metal, so changing it part-way through a frame forces a pipeline or
encoder change. `draw` executes immediately in OpenGL and is recorded in the
other two, though `submit(DrawSubmission)` already has the record shape for
exactly this reason. `readback` of the centre pixel is a synchronous copy in
OpenGL and a copy to a host-visible buffer plus a fence elsewhere, and it has
been explicit since the presentation change, so the contract does not assume
the value is already there. The default framebuffer being unsampleable is an
OpenGL fact rather than a general one: Vulkan and Metal both allow sampling the
presented target, which would remove the reason the post chain ends in a
separate display target.

The honest position is that none of this is verified. It is a list of the
places where a second backend is expected to press on the contract, written
before the backend exists so that the prediction can be checked against it.

## 5. Licensing boundary

This section records what may and may not be done, so the boundary is explicit
rather than assumed.

**What is public and usable.** The feature documentation, the licence terms, the
ModDB entry, the developer's forum posts, and the AngelScript project's list of
applications are all public. Architectural facts drawn from them — module
decomposition, subsystem responsibilities, backend choices, rendering
techniques — may be described and implemented freely.

**What is not.** The GRACE 2 SDK was distributed under a free **non-commercial**
licence. Its headers, libraries, and any content derived from them may not be
incorporated into this project, redistributed, or used as an implementation
source. This project is Apache-2.0, which grants unrestricted commercial rights
to recipients; that grant cannot be extended to material the original rights
holder never licensed for redistribution.

**Consequences for this repository.** No SDK file, header, excerpt, symbol, or
translation may be committed. Architectural knowledge from the SDK stays out of
the public tree. Public documentation is cited; private material is not.

**Unrelated game-format work.** This project reimplements the GRACE 2 engine
architecture. Work on shipped game data formats belongs to a separate,
independent effort with its own repository, scope and licence boundary, and
the two should not be conflated: nothing in this repository reads, writes or
documents those formats.

---

## 6. Milestone plan

This is the original coarse plan, kept as the record of intent. The
authoritative progress record, with finer-grained numbering and validation
results, is [implementation-status.md](implementation-status.md); its
milestone numbers do not correspond one to one with the list below.

Sequenced so that each milestone is independently testable and leaves the tree
buildable.

1. **Core skeleton.** Module layout, application lifecycle, fixed-step loop,
   deterministic tests. Partially exists.
2. **Render contract and OpenGL backend.** Platform-neutral renderer interface,
   null and OpenGL implementations. Partially exists.
3. **Forward lighting.** Point, spot and directional lights; four dynamic lights
   per object plus sunlight; shadow mapping.
4. **Deferred lighting.** G-buffer, unlimited lights, light volume rendering.
5. **Post-processing chain.** HDR, bloom, filmic tonemapping, depth of field
   with bokeh, morphological anti-aliasing, SSAO-STF.
6. **Global illumination.** Light volume propagation.
7. **Physics contract and Bullet backend.** Rigid bodies, joints, buoyancy,
   force fields, raycasts, character controller.
8. **Script host.** AngelScript integration, type registration, serialisation
   strategy.
9. **Visual scripting.** Tetragramaton block registry and graph evaluation.
10. **Animation.** Skeletal keyframes, state machines, layer blending, node
    controllers, events, blendshapes.
11. **Scene management.** Data-driven entities, selectable spatial partitioning,
    scene queries, LOD.
12. **Audio.** 2D/3D playback, streaming, reverb, OpenAL Soft backend.
13. **UI.** Retained controls, FreeType fonts, unicode input.
14. **AI and gameplay.** Agent behaviour, gameplay services.

---

## 7. Open questions

- ~~Which rendering path is primary: forward, deferred, or both selectable at
  runtime as in the original?~~ Resolved: forward rendering is the primary
  path, matching the documented per-object model of four dynamic lights plus
  sunlight. The deferred path stays planned as a later milestone rather than
  a runtime alternative. See docs/implementation-status.md.
- Does the Tetragramaton block set need to be reproduced faithfully, or is a
  functionally equivalent block vocabulary acceptable?~~ Resolved: a
  functionally equivalent block vocabulary is accepted. The original block
  set is not publicly documented, so the contract's registry holds block
  descriptions and the graph references them by name; the vocabulary is an
  engine-owned set of native functions registered through the script
  backend's registerFunction surface. See docs/implementation-status.md.
- What is the save format's relationship to script state, given that AngelScript
  has no built-in object serialisation?~~ Resolved: the scene's state lives
  in engine-owned structures and the serialisation writes it into the OWD
  container; the script state holds behaviour rather than authoritative
  state, and the save file does not serialise script objects. See
  docs/implementation-status.md.
- Is the original GRACE 2 codename scheme to be preserved for milestone naming,
  or is that an unnecessary affectation?~~ Resolved: it is not preserved.
  The milestone numbering (4b, 4c, the d-g increments) is the project's own
  numbering, not the original's, and the original engine's internal codenames
  are not reproduced. See docs/implementation-status.md.
- Should the engine name remain OpenGrace, given the precedent set by OpenMW,
  OpenRCT2, OpenTTD and OpenSAGE?~~ Resolved: the engine name stays OpenGrace.
  The convention the four open-source reimplementation projects set is the
  one this project follows: the name carries the original product's name and
  states the project's intent. See docs/implementation-status.md.

---

## Sources

All publicly accessible as of this writing.

- GRACE 2 official site, archived 2 November 2012:
  `web.archive.org/web/20121102032243/http://grace2engine.com/`
- GRACE 2 licence page, archived:
  `web.archive.org/web/2012/http://grace2engine.com/license/`
- GRACE 2 features page, archived:
  `web.archive.org/web/2013/http://grace2engine.com/features/`
- GRACE 2 download page, archived:
  `web.archive.org/web/2013/http://grace2engine.com/download/`
- GRACE 2 engine entry, ModDB:
  `moddb.com/engines/grace-2`
- "GRACE2 engine — Our game engine using AngelScript", GameDev.net, May 2012:
  `gamedev.net/forums/topic/624869/`
- Applications using AngelScript, AngelCode:
  `angelcode.com/angelscript/users.html`
- AngelScript licence:
  `angelcode.com/angelscript/sdk/docs/manual/doc_license.html`
- PhysX 3.4 open-source release, BSD-3-Clause, December 2018:
  `github.com/NVIDIAGameWorks/PhysX-3.4`
- AngelScript serialisation and reflection reference:
  `angelcode.com/angelscript/sdk/docs/manual/doc_serialization.html`
