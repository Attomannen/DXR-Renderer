# DXR Renderer: Portfolio Showcase Roadmap

Everything the DX12/DXR renderer has today, everything it is missing to be an
art-showcase portfolio piece, and the order to do it in. Written 2026-09-18
from source, not from memory: every "exists" claim below has a file reference
and every "missing" claim was confirmed by a repo-wide search.

**Revised 2026-09-18, later the same day**, after a working session that closed
part of section 2 and added a day-night cycle and a cloud system the original
document did not anticipate. Statuses below were re-checked against the code,
not carried over. Where a fix turned out differently from what the original
entry guessed, the entry says so -- the guesses are left visible on purpose,
because several of them were wrong in instructive ways.

Companion documents, still valid and not repeated here:
- `PORTFOLIO_ROADMAP.md` - measured costs, hardware caveats, SHARC / SER plan.
- `DXR_RENDERER_AUDIT.md` - the measurement protocol and the per-ray cost audit.
- `Source/Graphics/tge/render/Lighting.md` - lighting conventions and validation.

---

## 0. What "portfolio art showcase ready" means

A reviewer spends two to five minutes. They will watch a video or click through
stills before they read a line of code. The renderer therefore has to win on:

1. **The image.** Physically plausible light, no visible noise, no leaks, no
   shimmer, a real tonemapping and lens pipeline, and at least one scene that
   looks *designed* rather than benchmarked.
2. **The motion.** A smooth authored camera path, stable temporal behaviour,
   no history smearing, a video that plays at a steady frame rate.
3. **The proof.** Side-by-side comparisons (RT on/off, denoised/raw, real-time
   vs path-traced reference), a profiler overlay, debug views that show the
   technique working.
4. **The story.** A README with hero shots, a one-paragraph architecture
   summary, numbers with hardware named, and a short list of what is novel.

The renderer already has most of (3) and much of (1). It has almost none of (2)
and (4), and it is missing the entire "lens" half of (1). That shapes the order
in section 7.

---

## 1. Current feature inventory (verified)

### 1.1 Ray-traced frame

| Feature | Where | State |
|---|---|---|
| Single inline-RayQuery mega-dispatch: primary, direct, shadows, AO, GI lookup, reflections | `DeferredRendererDxr.cpp:314`, `DxrLightingCS.hlsl:243` | Complete. Monolithic; one GPU timer for everything |
| Bindless scene (geometry, textures, TLAS in separate spaces) | `DxrCommon.hlsli:67-119` | Complete |
| Per-instance FORCE_OPAQUE from material classification, cutout alpha test at 0.33 | `RayTracingMaterialTable.h:46`, `DxrCommon.hlsli:260` | Complete |
| Sun: 1-4 stratified disc samples, two-sided occluders, primitive-only self rejection | `DxrCommon.hlsli:356` | Complete |
| Punctual lights (point / spot) with shadow rays | `DxrCommon.hlsli` `EvaluatePunctualLight` | Complete, but loops all lights, no culling |
| Emissive triangles as area lights: gather pass, RIS, temporal + spatial ReSTIR DI, one shadow ray on the survivor | `DeferredRendererDxr.cpp:281`, `DxrCommon.hlsli:947-1120` | Working. No Jacobian / MIS on spatial taps, no neighbour similarity test, no light BVH, list only rebuilt on instance-count change |
| AO: 1-8 hemisphere rays, quadratic falloff, modulates sky and GI only | `DxrLightingCS.hlsl:139` | Complete |
| Reflections: GGX importance sampled, roughness-scaled 1-8 spp, one bounce, full shade at hit, virtual-image motion vectors | `DxrCommon.hlsli:1259`, `DxrLightingCS.hlsl:475-516` | Complete. Single bounce; hit shading is the top cost on glossy scenes |
| GI: uniform SH L2 probe volume, octahedral depth moments with Chebyshev visibility, batched capture, infinite-bounce feedback | `DeferredRendererGi.cpp`, `GiTraceInlineCS.hlsl`, `DxrCommon.hlsli:484-504` | Mature. One uniform grid, no relocation, no classification, no cascades |
| Ray-cone texture filtering, 16x anisotropic SampleGrad | `Lighting.md` "Ray texture filtering" | Complete |
| Specular anti-aliasing from normal variance | `Lighting.md` "Specular anti-aliasing" | Complete |
| Physical sky (Hillaire 2020): transmittance, multi-scatter and sky-view LUTs, sun-dirty tracking, night sky blend | `DeferredRendererSky.cpp`, `Sky*LutCS.hlsl` | Complete. Sky cubemap is only 128 px per face; no aerial perspective from the LUTs |
| Height fog + ray-marched, RT-shadowed volumetric sunlight, bounded march, render-res variant under DLSS | `DeferredRendererAtmosphere.cpp:63`, `AtmosphereVolumeRTCS.hlsl` | Complete. Global medium only, no froxels, no local volumes |
| Glass: forward pass, screen-space refraction, Beer absorption, per-material IOR / thickness | `GlassModelShaderPS.hlsl`, `MaterialParams.hlsli:49-52` | Works. Glass is invisible to rays: no ray shadow, absent from reflections, composited before fog |

### 1.2 Temporal, denoise, upscale

| Feature | Where | State |
|---|---|---|
| NRD 4.17: REBLUR and RELAX, diffuse + specular, demodulation, checkerboard, antilag, validation overlay | `NrdWrapper.cpp`, `DeferredRendererDxr.cpp:499`, `NrdCompositeCS.hlsl` | Complete and the strongest part of the frame |
| DLSS SR (4 modes), DLAA, presets, Ray Reconstruction with full guide set | `DeferredRendererDxr.cpp:92-100, 631-697` | Complete. RR measured too expensive on the 60 W laptop |
| Native TAA: depth + motion rejection, stationary mode, YCoCg clamp | `TemporalResolveCS.hlsl`, `DeferredRendererDxr.cpp:712-753` | Complete |
| **TAA jitter** | `DeferredRendererDxr.cpp:353` | **Forced to `{0,0}`.** The 20-line comment above it explains why it should be on, and `Tunables::taaJitter` is dead. The audit addendum recorded jitter enabled and measured a 35% edge-aliasing win, so this is a regression, probably from the DLSS wobble fix |
| Motion vectors, depth, validity written by the ray pass on hit and miss | `Lighting.md` "Temporal input foundation" | Complete |

### 1.3 Camera, exposure, post

| Feature | Where | State |
|---|---|---|
| Physical camera: aperture / shutter / ISO to EV100, auto metering via log-luma chain, EV comp, pre-exposure | `Photometry.h`, `DeferredRendererPostFx.cpp:111-135`, `DeferredRendererDxr.cpp:34` | Complete |
| Bloom: 6-mip prefilter / down / tent-up with threshold and knee | `DeferredRendererPostFx.cpp:86-105` | Complete |
| Tonemap: AgX, AgX Punchy, ACES, none | `PostFxCommon.hlsli`, `DeferredCompositePS.hlsl` | Complete. ACES curve was recovered from a disassembled `.cso`; no source of truth |
| Auto exposure, partially compensating | `ExposureAdaptPS.hlsl`, `DeferredRenderer.h` | Complete. On by default at 0.25 strength. A fully compensating meter makes every scene the same screen brightness and cancels a day-night cycle out: measured night mean over day mean was 2.02 at full strength, 0.84 at 0.25 |
| Depth of field | none | **Missing** |
| Motion blur | none | **Missing** |
| Colour grading (LUT, lift/gamma/gain, white balance) | none | **Missing** |
| Vignette, film grain, chromatic aberration, lens dirt / flare, sharpening | none | **Missing** |
| HDR10 / scRGB output | none | **Missing** |

### 1.4 Materials

| Feature | State |
|---|---|
| Four maps (BaseColor+opacity, Normal, ORM, Emissive), Unreal packing, 80-byte shared parameter block | Complete, `MaterialParams.hlsli:40-57` |
| Shading models | `DefaultLit`, `Glass` only |
| Emissive as light source | Yes, via the ReSTIR gather |
| Clearcoat, sheen / cloth, anisotropy, subsurface, thin-film, hair | **Missing** |
| Parallax / displacement, detail maps, per-material tiling and offset, UV2 | **Missing** |
| Material Graph editor (uncommitted) | Offline CPU texture baker with 11 node types, bakes to `_C/_N/_M/_FX.dds`. Nearest-neighbour sampling only. Not a runtime shader graph |

### 1.5 Content and scene

| Item | State |
|---|---|
| Scenes | Sponza, Bistro exterior, TEST, a built-in sealed room |
| HDRI | `.hdr` load and prefilter (`CubemapPrefilter.cpp:304`), one 4k meadow HDRI shipped |
| Skinned animation | Engine has it, nothing animated is in any DXR scene, no BLAS refit |
| Particles | Raster-only sprite emitter, absent from the DXR path |
| Decals, terrain, water, vegetation wind | None |

### 1.6 Tooling and proof

| Item | State |
|---|---|
| 19 DXR lighting debug views, G-buffer views, TAA views, atmosphere views, NRD validation | Unusually thorough |
| GPU / CPU profiler with 240-frame history, VRAM bar, copy report, automated per-feature cost sweep | Excellent, `GameWorldProfiler.cpp:219-370` |
| Bench harness: ~50 `BENCH_*` vars, JSON report, PNG capture, fixed / spin / orbit / room cameras, freeze frame | Complete for measurement, not for presentation |
| Camera paths, sequencer, video capture, split-screen compare | **Missing** |
| Editor viewport | Ray-traced when the device supports it (`DefaultEditorGraphics.cpp:620`); no irradiance volume, so indirect differs from the game |

---

## 2. Bugs and correctness debt to clear first

A showcase cannot carry visible defects. In priority order. Statuses re-checked
against the code on 2026-09-18 after the session that closed items 1, 2 and 7.

1. ~~**TAA jitter forced to zero**~~ **DONE, with a caveat.** (`DeferredRendererDxr.cpp:353`). Restore the
   Halton jitter and re-verify the DLSS wobble fix still holds with it on.
   Jitter is on for DLSS, DLAA and Ray Reconstruction. It is deliberately left
   off for the *native* temporal resolve, which measurably falls apart under
   it: on a completely static camera, 7000 pixels per frame change by more than
   30/255 with jitter on against 25 with it off. DLAA under the same jitter is
   stable at 22, so this is the native resolve's bug, not the jitter's. That
   defect is not tracked anywhere else and belongs on this list.
2. ~~**Bistro glass renders as flat grey noisy panels**~~ **DONE, and neither
   guess was right.** It was not a misclassification and not the wrong HDR
   copy: the asset import had written no material definitions at all, so every
   mesh fell back to a default. Regenerating them from the cook report fixed
   it. Two real bugs surfaced underneath, both now fixed: the material apply
   loop passed the *positional* path alongside the name-resolved definition, so
   meshes sharing a material got separate records and meshes colliding on an
   index shared the wrong one; and the loop was bounded by the material list's
   length as well as the mesh count, leaving every mesh past the end of the
   list untextured.
3. **Glass is invisible to rays.** `kRayTransparent` instances are skipped
   entirely, so glass casts no shadow, tints nothing behind it, and vanishes
   from reflections. Short term: include glass in shadow rays as a coloured
   attenuator. Long term: section 3.3.
4. **Glass is composited before fog** (graph order in `BuildFrame`). Any window
   in the middle distance is unfogged against a fogged wall. Worse than
   written: the transparent pass depth-tests at *display* resolution against a
   depth buffer point-upsampled from *render* resolution, so its silhouettes
   are quantised into render-resolution blocks while the glass itself
   rasterises at display resolution. Under DLSS upscaling a one-pixel glass
   edge then passes the depth test on some jitter phases and fails on others,
   and because the shader divides its additive reflection by opacity (a
   six-fold amplification at the Bistro windows' 0.16) that edge flashed to
   white for single frames. The flash is capped; the resolution mismatch is
   not fixed. Sizing the cloud and transparent passes to the render resolution
   under upscaling is the real cure.
5. **ReSTIR DI spatial reuse is biased.** PARTLY FIXED: taps are now rejected
   across surfaces on normal and plane distance, which was the visible half --
   the light bleeding across depth edges next to emissive geometry. Still no
   Jacobian and no MIS weighting on the three spatial taps, so it remains
   biased.
6. **Emissive light list only rebuilds on instance-count change**
   (`DeferredRendererDxr.cpp:286`). Any moving or animated emitter lights its
   old position. Rebuild on any emissive transform change, or every frame for
   dynamic instances.
7. ~~**Specular ReSTIR contribution is added un-denoised**~~ **DONE.** It is
   written into the NRD specular signal.
8. **DX11 backend crashes on exit** in `Dx11Device::WrapNativeSrv`. Not a
   showcase issue, but the launcher offers DX11 and a crash is a bad first
   impression. Either fix or hide the DX11 option from the launcher.
9. **Editor has no irradiance volume.** Indirect light in the editor viewport
   is ambient-only, so what an artist authors is not what the game shows.
10. **One CPU stall per environment change** for the luminance readback
    (`DeferredRendererDxr.cpp:59-66`). Matters once time-of-day animates.
11. **ACES curve has no source.** Re-derive from the published fit (Narkowicz
    or Hill) so the tonemapper is auditable.
12. **The native temporal resolve cannot handle jitter.** See item 1. Until
    this is fixed, the native path is the low-quality option and DLAA is the
    only way to get a converged image at 1:1.
13. **The cloud and transparent passes render at display resolution while the
    frame renders below it.** Both should follow the render resolution under
    upscaling: the clouds are resampled down and back up for nothing, and the
    transparent pass's depth mismatch is item 4.
14. **Star brightness is absolute rather than metered**, so once auto exposure
    reaches its floor the stars carry the frame mean up and deep night reads
    brighter than dusk.

---

## 3. Rendering features

Grouped by what they buy on screen. Each has a rough effort (S under a day,
M a few days, L a week or more) and dependencies.

### 3.1 Lens and post-process pipeline (the largest visible gap)

These are what make a ray-traced frame read as a *photograph* rather than a
tech demo. None exist today.

| Feature | Notes | Effort |
|---|---|---|
| **Physical depth of field** | Circle of confusion from the existing aperture and focal length; gather-based bokeh at half res with near/far split; focus distance from a screen-space pick or autofocus at centre depth. The physical camera already gives f-number, so the DoF is free of new UI. | M |
| **Motion blur** | Per-pixel velocity from the existing motion vectors, tile max / neighbour max, shutter angle from `cameraShutter`. Camera-only blur is a day; object blur needs the same vectors and is nearly free. | S-M |
| **Colour grading** | 3D LUT (`.cube` load) applied after tonemap in the composite; lift / gamma / gain, saturation, contrast, white balance in Kelvin, and a per-scene grade in the `.tgs`. | S-M |
| **Vignette, film grain, chromatic aberration** | All in `DeferredCompositePS.hlsl`. Grain must be applied after tonemap and be temporally stable (blue noise, not white). | S |
| **Lens flare and lens dirt** | Screen-space ghosts / halo from the bright bloom mips plus a dirt texture masked by bloom. Cheap and very "showcase". | S-M |
| **Sharpening** | CAS-style contrast-adaptive sharpen after upscaling. DLSS SR modes look soft without it. | S |
| **Local tone mapping / exposure fusion** | Optional. AgX handles most scenes; skip unless interiors with bright windows are a target. | M |
| **HDR10 / scRGB output** | Swapchain to `R10G10B10A2` or `R16G16B16A16_FLOAT`, ST.2084 PQ encode, paper-white and peak-nit sliders, keep AgX in the linear domain. The physical exposure pipeline shines on an HDR monitor. | M |
| **Post-process ordering pass** | Fix the chain to: fog -> denoise / upscale -> DoF -> motion blur -> bloom -> exposure -> tonemap -> grade -> grain -> sharpen -> UI. | S |

### 3.2 Direct lighting

| Feature | Notes | Effort |
|---|---|---|
| **Finish ReSTIR DI** | Jacobian-corrected spatial taps, normal / depth / roughness similarity rejection, M-cap tuning, boiling filter. Fixes bug 5. | M |
| **Light BVH or ReGIR** | Replaces the uniform triangle pick in candidate generation (`DxrCommon.hlsli:1010-1016`). Needed for a night Bistro with hundreds of lamps, which is the obvious hero shot. | L |
| **Punctual lights through ReSTIR** | Fold point / spot lights into the same reservoir so light count stops costing shadow rays, and drop the 1024-light loop. | M |
| **Clustered light culling in DXR mode** | The raster cluster pass exists and is skipped for DXR. Cheap interim before the item above. | S |
| **Area light shapes** | Sphere, disc, rect, tube with LTC or ReSTIR sampling. Emissive meshes already cover most cases; needed mostly for authoring convenience. | M |
| **IES profiles** | Photometric light profiles as a 1D or 2D texture. Small, and very convincing on lamps. | S |
| **Sun soft shadow from angular diameter** | Already sampled; expose the angular radius as the single sun-softness control across raster and DXR (currently raster has its own). | S |
| **Shadow terminator** | Hanika offset is cited (`DxrCommon.hlsli:816`); verify it is applied to all shadow rays, including GI capture. | S |

### 3.3 Transmission, refraction, caustics

| Feature | Notes | Effort |
|---|---|---|
| **Ray-traced refraction** | Glass becomes a DXR hit that continues the ray with Snell refraction, Fresnel split, Beer absorption over the traced thickness, roughness blur on exit. Removes the screen-space refraction and its edge artefacts. The material data already exists. | L |
| **Coloured shadows through glass** | Shadow rays accumulate transmittance through `kRayTransparent` hits instead of skipping them. Do this first; it is a day and fixes bug 3. | S |
| **Glass in reflections** | Falls out of ray-traced refraction. | - |
| **Caustics** | Expensive. If wanted, photon-mapped or ReSTIR-PT caustics for one hero object only. Otherwise skip and say so. | L |
| **Thin vs solid glass** | Thin uses the existing thickness parameter, solid needs the back-face hit. Cover both in the material flag. | M |

### 3.4 Indirect lighting

| Feature | Notes | Effort |
|---|---|---|
| **Path-traced reference mode** | Unbiased progressive path tracer over the same bindless scene and materials, accumulating while the camera is still, with an A/B wipe against the real-time frame and an on-screen error metric. This is the single strongest proof item: it demonstrates that the physical units, exposure and materials are correct, not just pretty. | L |
| **SHARC radiance cache** | Replaces the full re-shade at reflection hits with a hash-grid lookup, and gives multi-bounce reflections and GI for free. Documented plan in `PORTFOLIO_ROADMAP.md` 3.2. | L |
| **Multi-bounce reflections** | Second bounce on near-mirror hits only, or via SHARC. Mirror-on-mirror is the classic showcase shot. | M (S after SHARC) |
| **ReSTIR GI** | Per-pixel indirect with real contact detail and no probe grid. Cost increase; do after SHARC. | L |
| **DDGI hardening** | Probe relocation, classification (skip enclosed probes), cascaded volumes around the camera, per-scene volume authored in the `.tgs` rather than a global. Keeps the probe path as the cheap fallback. | M |
| **Emissive in GI** | Already stored in the probe SH; verify large emitters also drive the infinite-bounce feedback once double-buffered (`DeferredRenderer.h:192-200`). | S |
| **Specular occlusion and bent normals** | Bent-normal AO from the existing AO rays for better sky occlusion; GTAO-style specular occlusion. Cheap quality. | S |

### 3.5 Materials

| Feature | Notes | Effort |
|---|---|---|
| **Clearcoat** | Second GGX lobe with its own roughness and normal. Car paint, lacquered wood. Highly visible in RT reflections. | M |
| **Anisotropy** | Stretched GGX with tangent-space direction. Brushed metal is the archetypal RT material showcase. Needs tangents at the hit, which `DecodeHit` already loads. | M |
| **Sheen / cloth** | Charlie or Ashikhmin sheen lobe. Bistro's awnings and Sponza's curtains benefit. | S-M |
| **Subsurface** | Screen-space diffusion or ray-traced random-walk for one hero asset. Only worth it with a character or a wax / marble asset in a scene. | L |
| **Thin-film iridescence** | Cheap, striking on soap-bubble or oil-slick assets. | S |
| **Parallax occlusion mapping / height maps** | Raster only. For DXR, real displacement is out of scope; POM at the primary hit is possible but usually not worth the divergence. | M |
| **Detail normal / albedo maps, per-material tiling and offset** | Small material-record change, large close-up quality gain. | S |
| **Alpha coverage mips for cutouts** | Coverage-preserving alpha mip generation in the cooker so distant foliage does not thin out. Noted as future work in `Lighting.md`. | S |
| **Material Graph: bilinear and mip-aware sampling** | The baker samples nearest-neighbour (`MaterialGraphBake.cpp:48-53`); bilinear plus box-filtered downsample makes bakes usable. | S |
| **Material Graph: more nodes** | Fresnel, noise (Perlin / Voronoi), gradient, remap, normal blend, texture UV transform, height-to-normal. Turns the baker into a real authoring tool. | M |
| **Runtime material variants** | Optional. A small ubershader permutation set (clearcoat on/off, aniso on/off) keeps the single `DecodeHit` path from growing per feature. | M |

### 3.6 Atmosphere and sky

| Feature | Notes | Effort |
|---|---|---|
| **Aerial perspective from the sky LUTs** | Replace the constant fog colour with the atmosphere's in-scatter and transmittance along the view ray (Hillaire's aerial-perspective LUT). Distant Bistro streets get real blue haze that changes with the sun. | M |
| **Time-of-day animation** | Sun pitch / yaw driven from a clock with the sky dirty-tracking already in place; keyframe exposure comp per hour. This one control gives an entire video segment. | S |
| **Higher-resolution sky cubemap** | 128 px per face is visible in sharp reflections of the sun; raise to 512 and prefilter accordingly. | S |
| **HDRI sun matching** | Extract the brightest direction and intensity from a loaded HDRI and drive the sun from it so HDRI and shadows agree. | S |
| **Froxel volumetric fog** | 3D scattering / extinction volume with temporal reprojection, local fog volumes (box / sphere), point and spot light scattering, fog affecting sky. The current global medium looks the same everywhere. | L |
| ~~**Volumetric clouds**~~ | **Done.** Ray-marched, 96 steps, temporally accumulated, composited before fog so the horizon haze washes them out. Two baked noise volumes (128^3 shape, 64^3 detail). Coverage, cloud type and tower height come from weather fields sampled at a fixed altitude, so they vary per column rather than per sample. Spherical shell with an exaggeration factor that trades recession against fullness. | done |
| ~~**Procedural stars**~~ | **Done.** `StarField.hlsli`, evaluated per pixel in the atmosphere composite rather than baked into the cubemap, where a star is far smaller than a texel. Occluded by clouds, extinguished toward the horizon, turns rigidly with the sun. | done |
| ~~**Day-night cycle**~~ | **Done.** Sun radiance, cloud lighting, fog colour and ambient all follow sun elevation; the night sky no longer blends the scene's daylight HDRI in at 5 percent. Night went from 22 percent darker than day to 80 percent. | done |
| **A moon** | The remaining gap at night: with the sun gone nothing lights the geometry, so buildings read as flat silhouettes. Correct, but dull. A directional light with the moon's illuminance and a disc in the sky. | S |
| **Star intensity tied to exposure** | Stars are absolute, so once the meter hits its floor they carry the frame mean up and -30 degrees is brighter than -10. | S |
| **Fog-aware GI and reflections** | Apply transmittance to reflected and GI radiance so fogged reflections do not read as sharper than the scene. | S |

### 3.7 Geometry, animation, dynamic content

| Feature | Notes | Effort |
|---|---|---|
| **BLAS refit for skinned meshes** | Compute-skinned vertex buffer plus `PERFORM_UPDATE` refit each frame; previous-frame positions for motion vectors. Unlocks any animated content in a ray-traced scene. | M |
| **An animated asset in a scene** | A character, a flag, a swinging lamp: anything moving proves the temporal pipeline in a way stills cannot. | content |
| **Vegetation wind** | Vertex animation for foliage needs refit; cheap once the above exists. | S |
| **Opacity micromaps** | Cuts cutout traversal cost; needs newer Agility SDK. Perf only. | M |
| **Decals** | Deferred decals on the raster path; for DXR, a decal atlas evaluated in `DecodeHit` by projecting the hit through a decal list. Optional. | M |
| **Particles in DXR** | Camera-facing sprites composited after the ray pass with the DXR depth, lit by the probe volume. They are absent today. | M |
| **Water** | Optional hero feature: an animated normal surface with RT reflection and refraction once 3.3 exists. | L |

### 3.8 Denoising and temporal quality

| Feature | Notes | Effort |
|---|---|---|
| **Restore jitter** | Bug 1. | S |
| **AO and shadows as separate NRD signals** | Use NRD's SIGMA for sun shadow and route AO through REBLUR's AO path instead of folding them into the diffuse signal. Cleaner penumbrae and lets sun samples drop to 1. | M |
| **NRD performance mode** | `REBLUR_PERFORMANCE_MODE` build; roughly a third off the denoiser. | S |
| **Disocclusion fill** | On history rejection, fall back to a higher sample count for that pixel for a few frames instead of showing noise. | S |
| **Responsive-input mask for glass and particles** | Lets the TAA and DLSS resolve treat transparency correctly rather than ghosting it. | S |

### 3.9 Performance items that also improve the image

| Feature | Notes | Effort |
|---|---|---|
| **Split the mega-dispatch** | Separate primary / shadow / AO / reflection passes. Gives honest per-pass timers for the profiler overlay in the video, per-effect resolution, and is the prerequisite for SER and ReSTIR GI. | L |
| **Shader Execution Reordering** | DXR 1.2 RayGen path with `MaybeReorderThread`; typically 10-30% on the divergent reflection shading. Depends on the split. | L |
| **Half-resolution reflections and AO with guided upsample** | Roughness-guided upsample; keep full rate only below roughness 0.1. | M |
| **Trilinear sampler for secondary rays** | Secondary hits pay for 16x anisotropy for nothing. | S |
| **TLAS `PREFER_FAST_TRACE`, batched BLAS builds, deferred compaction** | Removes load hitches; needed once streaming or animation exists. | S-M |

---

## 4. Presentation and proof infrastructure

This is what turns a renderer into a portfolio. None of it exists.

### 4.1 Cinematic camera
- **Spline camera paths** authored in the `.tgs`: Catmull-Rom or Bezier over
  position, look-at, FOV, focus distance and aperture, with ease in/out and a
  fixed duration. Save from F5-style viewpoints, edit in the tuning panel.
- **Camera cuts** that call `ResetTemporalHistory()` and pre-warm exposure so
  the first frame of a shot is not a fade-in.
- **Deterministic playback**: fixed timestep when recording so a 24 fps render
  and a 60 fps preview follow the same path.
- **Orbit and dolly presets** around a selected object for material shots.

### 4.2 Video capture
- **Frame dump mode**: render at a fixed timestep to numbered PNG or EXR
  (16-bit HDR for later grading), independent of real-time speed, with UI
  hidden. A batch file invokes `ffmpeg` to encode H.264 / H.265 and an HDR10
  variant.
- **Burn-in option**: scene name, GPU and wattage, resolution, DLSS mode,
  frame time, in a corner. Reviewers trust numbers they can see in the video.
- **GIF / WebP exporter** for the README hero shots, from the same dump.

### 4.3 Comparison tools
- **Split-screen wipe**: A/B any two `Tunables` states (RT reflections vs
  environment, NRD vs raw, real-time vs path-traced reference, 1 spp vs
  converged) with a draggable divider and labels. The single most effective
  showcase device.
- **Debug-view cycling** during a path: hold a shot and fade through albedo,
  normals, AO, direct, GI, reflections, final. Lets a video explain the frame
  without narration.
- **Error metric overlay** against the reference mode: FLIP or RMSE, per-frame,
  so "close to reference" is a number.
- **Screenshot with sidecar JSON** of the full `Tunables` and camera so any
  still is reproducible.

### 4.4 Profiling overlay for video
- Compact GPU pass bar (needs the split dispatch for meaningful bars),
  frame time graph, resolution and upscaler mode, ray count per frame.
- Toggleable with one key; styled to match the HUD rather than ImGui default.

### 4.5 Presets
- **Quality presets** (Showcase / Balanced / Performance) as a settings asset
  rather than 50 environment variables, with per-scene overrides. The
  editor-usability doc already lists this.
- **Scene showcase presets**: per scene, a camera path, a time of day, a
  grade, and a `Tunables` set that is known to look right.

---

## 5. Content

A renderer is judged on the scenes it ships. Sponza and Bistro exterior are
fine as benchmarks, and everyone has seen them. Add, in order of value:

1. **Bistro at night** with the shop and street lamps emissive: proves ReSTIR
   DI on hundreds of lights, the physical sky at night, bloom and lens flare.
   Same assets, one new lighting rig and a grade. Cheapest big win.
2. **A material showroom**: a small designed set (turntable, studio HDRI,
   three-point lights) with spheres and hero props for clearcoat, anisotropy,
   sheen, glass, iridescence, subsurface. This is where every new material
   feature gets its shot.
3. **An interior with a bright window**: proves exposure, GI colour bleed,
   sun shafts and glass in one frame. Sponza's atrium already does half of it;
   Intel Sponza's curtains and the empty `Intel-Sponza.leveldata` are a start.
4. **Something animated**: a character walking through Bistro, or a moving
   vehicle with headlights. Depends on BLAS refit.
5. **HDRI-lit exterior** with a matched sun for the "physically based" claim.

For each scene: an authored camera path, a grade, and a locked screenshot set.
Credit the asset sources (Intel, Amazon Lumberyard, Poly Haven) in the README.

---

## 6. Editor and tooling

Only what a showcase needs; the full editor list is in
`EDITOR_USABILITY_PROGRESS.md`.

- **Irradiance volume in the editor viewport** so authored lighting matches
  the game (bug 9).
- **Light gizmos and drag editing** in the viewport, including emissive
  intensity in nits and colour temperature in Kelvin, with the ReSTIR list
  rebuilding live.
- **Camera path editor**: place, reorder and scrub keys; preview the path.
- **Material graph**: bilinear sampling, live preview on the matball, more
  node types, and commit the current work.
- **Quality settings asset** with override tracking (4.5).
- **Screenshot and video buttons** in the tuning panel so a capture does not
  need environment variables.

---

## 7. Suggested order

Ordered by visible payoff per week. Each phase ends with a new set of stills
and a short clip, so the portfolio improves continuously rather than at the end.

### Phase 1: make the current frame clean (1-2 weeks) -- MOSTLY DONE
1. ~~Restore TAA jitter~~ done for DLSS/DLAA/RR. Left off for the *native*
   resolve, which measurably falls apart under jitter: 7000 pixels per frame
   swinging on a still camera against 25 with it off. That is its own defect
   and is not tracked anywhere else in this document.
2. ~~Fix Bistro glass~~ done, but not for the reason item 2 of section 2
   guessed. Coloured shadows through glass and glass-after-fog are still open.
3. ~~Specular half into NRD~~ done. ReSTIR spatial reuse now rejects taps
   across surfaces, which was the visible half, but still has no Jacobian or
   MIS. Emissive list rebuild untouched.
4. NRD performance mode, trilinear secondary sampler -- untouched.
5. Commit the Material Graph work -- untouched.

Also done in this phase, unplanned: a performance pass that took the Bistro
frame from 32.8 ms to 20.1 (sun shadow rays 4 to 1 after fixing a sampler that
only randomised one of the disc's two polar coordinates, AO rays 2 to 1, fog
march to quarter resolution), and the whole atmosphere section above.

### Phase 2: the lens (1-2 weeks) -- NEXT, and still entirely untouched
6. Post-process reorder, then DoF, motion blur, colour grading LUT, vignette,
   grain, chromatic aberration, lens flare and dirt, sharpen.
7. ~~Time-of-day animation~~ the lighting side is done; what remains is driving
   the sun from a clock and authoring the exposure compensation curve.
8. Bistro at night lighting rig. More interesting now that the cycle works:
   at night nothing lights the geometry at all, so this and the moon above are
   really the same task.

### Phase 3: the proof (1-2 weeks)
9. Spline camera paths, camera cuts, deterministic frame dump, ffmpeg script.
10. Split-screen wipe and debug-view cycling.
11. Path-traced reference mode with the error overlay.
12. README with hero GIFs, the architecture paragraph, hardware-qualified numbers.

At the end of Phase 3 the renderer is portfolio-ready. Everything after is
depth.

### Phase 4: materials and transmission (2-3 weeks)
13. Clearcoat, anisotropy, sheen, thin-film; the material showroom scene.
14. Ray-traced refraction, solid and thin glass.
15. Aerial perspective, higher-res sky cubemap, HDRI sun matching.

### Phase 5: architecture (3-5 weeks)
16. Split the dispatch, separate NRD signals for shadow and AO.
17. SHARC, then multi-bounce reflections.
18. BLAS refit and an animated asset.
19. SER on DXR 1.2, light BVH, ReSTIR GI.
20. Froxel fog with local volumes; HDR10 output.

---

## 8. Reviewer checklist

Before calling it done, confirm each of these from the outside, as a stranger:

- [ ] A 60-90 second video with an authored camera, no UI, steady frame rate,
      burn-in showing hardware and resolution.
- [ ] Six hero stills, at least two scenes, at least one night and one interior.
- [ ] One split-screen comparison for each headline technique: ReSTIR DI,
      RT reflections, GI, denoising, DLSS, path-traced reference.
- [ ] README top section readable in one minute: what it is, what is novel,
      numbers with GPU name and wattage, links to the video and stills.
- [ ] No visible noise, shimmer, leaks, or ghosting anywhere in the video.
- [ ] Every scene loads from a fresh clone with one script and no manual steps.
- [ ] The DX11 launcher option either works or is gone.
- [ ] The Material Graph and any other uncommitted work is in git.
