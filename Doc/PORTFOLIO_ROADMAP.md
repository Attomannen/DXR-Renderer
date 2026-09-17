# DXR Renderer: Portfolio Roadmap

Where the DX12/DXR renderer stands, what to clean up before showing it, and
what to build next. Ordered by priority within each section.

## Current state (September 2026)

| Area | Status |
|---|---|
| Renderer | Single inline-RayQuery compute pass (primary, direct + shadows, AO, GI lookup, reflections) |
| Denoising | NVIDIA NRD (REBLUR default, RELAX optional), checkerboard option; DLSS / DLAA / Ray Reconstruction |
| Temporal | Native TAA (default, most stable), jitter-free motion vectors |
| Lighting units | Physical: sun in lux, sky and emissive in cd/m², point/spot lights in lumens/candela |
| Camera | Aperture / shutter / ISO → EV100, metered auto exposure, EV comp, pre-exposure |
| Tonemapping | AgX (default), AgX Punchy, ACES |
| GI | Ray-traced SH probe volume |
| Tooling | Profiler tab (CPU/GPU scopes, VRAM, feature cost sweep), load profile, bench harness (`BENCH_*`) |
| Loading | Batched GPU uploads, parallel texture decode, compact mesh cache (Bistro ~12 s startup) |

Reference numbers (RTX 5070 Laptop, 1600×900, native TAA + NRD):

| Scene | GPU frame | Ray trace + shade | NRD |
|---|---|---|---|
| Sponza | ~10–12 ms | ~8–11 ms | ~3 ms |
| Bistro | ~26–32 ms | ~18–24 ms | ~5 ms |

---

## 1. Refactors (do first)

### 1.1 Split `DeferredRenderer` *(steps 1–2 done; step 3 open)*
- **Problem:** ~2 700-line `.cpp` mixing raster, DXR, NRD, DLSS, fog, post-FX and GI.
- **Goal:** one file per concern, so each pass can be read on its own.
- **Steps:**
  1. Move the DXR renderer path (lighting pass, NRD, environment measurement,
     exposure history) into its own translation unit.
  2. Move atmosphere / fog and post-FX into their own translation units.
  3. Later: turn the DXR path into a `DxrRenderer` class that owns its targets,
     with `DeferredRenderer` only orchestrating.
- **Done:** `DeferredRenderer.cpp` (core, ~1 230 lines), `DeferredRendererDxr.cpp`,
  `DeferredRendererGi.cpp`, `DeferredRendererAtmosphere.cpp`, `DeferredRendererPostFx.cpp`,
  with shared constant buffers and helpers in `DeferredRendererInternal.h`.

### 1.2 Rename "smoke test" leftovers *(done)*
- `DxrSmokeTestCS` → `DxrLightingCS`
- `RenderDxrSmokeTest` / `ResolveDxrSmokeToHdr` → `RenderDxrLighting` / `ResolveDxrLightingToHdr`
- `myDxrSmoke*` members → `myDxrLighting*`
- Longer term: `DX11::` is the backend facade even under DX12 → rename to a neutral `Gfx::` or similar.

### 1.3 Named constant-buffer fields *(done for the DXR lighting pass)*
- **Problem:** fields reuse old padding names (`pad1`, `pad2[0]`, `pad5[1..2]`, `pad6`, `pad7`).
- **Goal:** every field named the same in C++ and HLSL, with `offsetof` checks.
- **Next step:** one shared header included by both C++ and HLSL, so they can't drift apart.

### 1.4 Break up `GameWorld.cpp` *(done)*
- `GameWorld.cpp` 3 486 -> ~930 lines (init / update / render), plus
  `GameWorldImpl.h` (private state), `GameWorldDebugUI.cpp`, `GameWorldProfiler.cpp`,
  `GameWorldGi.cpp`, `GameWorldScene.cpp`, `GameWorldCamera.cpp` and
  `SceneFiles.h/.cpp` (`.tgs` / `.tgo` / `.tgmat` loading).
- `BenchConfig.h/.cpp`: every `BENCH_*` variable the game reads is parsed there
  (startup, content, world and renderer overrides, plus a `Run` struct for
  one-shot capture / camera / sweep settings).
- `GiProbeScheduler.h`: priming, round-robin trickle, batch sizing and the
  low-hysteresis lighting-refresh sweep. The unused dynamic-emitter
  prioritisation path was removed.

### 1.5 Compact vertex format *(done)*
- Static meshes upload `MeshVertex` (40 bytes, was 180): position + bitangent
  sign, octahedral normal and tangent (snorm16), UV0, UV1, colour 0 (unorm8).
  Vertex buffers shrink about 4.5x.
- Skinned meshes keep the full `Vertex`; `Model::VertexFormat` records which one a
  mesh uses, `ModelShader` picks the matching input layout from its vertex shader
  and skips mismatched meshes.
- One upload path (`Model::CreateVertexBuffer`) for primitives, FBX import,
  the async importer and the mesh cache, which now packs straight into mapped
  upload memory. The FBX loop no longer uploads every sub-mesh before merging.
- DXR decodes the packed frame from the geometry record's `vertexFormat`.
- Known, unrelated: the raster renderer rendered black on DX12 at the time;
  fixed since (see "Raster deferred and the editor viewport").

### 1.6 Split the ray-tracing dispatch
- **Goal:** separate primary / G-buffer, shadow, AO and reflection passes.
- **Enables:** per-effect resolution, accurate GPU timers, independent budgets.

### 1.7 Commit hygiene
- Commit in logical pieces: NRD, photometry, pre-exposure, profiler, load time,
  jitter / motion-vector fixes, refactors.

---

## 2. Portfolio features

| Feature | Why it stands out | Builds on |
|---|---|---|
| Path-traced reference mode | Proves the real-time image is physically right; A/B toggle | Physical units, pre-exposure |
| ReSTIR DI (many lights) | Flagship modern RT technique; Bistro lamps become real lights | Split dispatch |
| DDGI / ReSTIR GI | Stable, leak-free indirect light | GI probe volume |
| Physical camera effects | Depth of field from aperture, motion blur from shutter | Camera model |
| Physical sky + atmosphere | Time of day with correct lux / EV | Photometry, sky measurement |
| Materials | ~~Unreal texture packing, per-material glass~~ (done, see below); clearcoat, foliage translucency, ray-traced transmission | Material table |
| HDR display output | Showcases AgX + physical exposure on HDR monitors | Tonemapper |

### Material system (done)
- One `MaterialParams` block (`EngineAssets/Shaders/MaterialParams.hlsli`,
  `Source/Core/tge/material/`) read by the G-buffer, forward, glass and editor
  shaders (b11) and by DXR hits (material table record).
- Unreal packing: `_BC`/`_D`/`_BaseColor`, `_N` (DirectX green, which the engine
  already used), `_ORM` (same order as the old `_M`), `_E` RGB emissive. Legacy
  `_C`/`_M`/`_FX` still resolve first; `.tgmat` gains tints/scales, emissive mode,
  normal convention and glass parameters.
- Glass is per material (IOR, refraction, thickness, absorption, opacity, rough
  blur) and now renders in the DXR renderer: its device depth is copied into the
  depth buffer and the forward pass composites after the ray-traced frame, with
  backface culling and the DXR environment for reflections.
- The debug sphere is a full material (Materials tab, or `BENCH_MATBALL_TGMAT`,
  `BENCH_MATBALL_SURFACE`, `_OPACITY`, `_ROUGHNESS`, `_METALNESS`, `_IOR`,
  `_REFRACTION`).
- Next: ray-traced transmission instead of screen-space refraction.

### Raster deferred and the editor viewport (done)
- DX12 raster deferred was black: the BRDF LUT was never bound for raster
  shading (ambient divided by zero), the SSR resolve cleared the environment
  slot and leaked its additive blend into later passes, and the composite left
  alpha undefined.
- The editor viewport renders through the deferred renderer (shadows, SSAO, SSR,
  bloom, glass) and through the ray-traced renderer where available, with scene
  point/spot lights, a TLAS rebuilt only on scene changes and throttled asset
  caches. `TGE_EDITOR_FORWARD` restores the old forward view.
- Open: no irradiance probe volume in the editor yet (ambient-only indirect);
  the DX11 backend crashes on exit in `Dx11Device::WrapNativeSrv`.

## 3. Performance work

| Item | Expected win |
|---|---|
| DLSS Quality / Performance + NRD | ~40–55 % of ray and denoise cost |
| NRD checkerboard AO + reflections | Bistro ray pass 23.8 → 17.6 ms (measured) |
| Separate instances / ray masks for alpha-tested foliage | Large on Bistro (skips the per-hit alpha loop); unblocked: the cooker now marks Masked materials |
| Cheaper reflection-hit shading, capped reflection ray length | Reflections are the largest single cost |
| Temporal reprojection for fog | Fog is ~4.5 ms on Bistro |
| NRD built with `REBLUR_PERFORMANCE_MODE` | Part of NRD's ~3–5 ms |

## 4. Tooling

- **Screenshot regression tests:** `BENCH_FREEZE_FRAME` + `BENCH_SHOT_COUNT` + image diff, run from a script or CI.
- **Profiler export:** save the Profiler tab report and feature-cost table to a file per run.
- **Write-up page:** before/after images, profiler screenshots, and the debugging stories:
  - NRD unit mismatch (centimetres vs metres)
  - jitter leaking into motion vectors
  - DX12 UAV table aliasing
  - material variants cooked as fully metallic
