# Lighting controls and validation

## Render tuning window

The scene picker and DXR switch stay at the top. Scrollable tabs group Render,
Lighting, Materials, Atmosphere, Post FX and Debug controls. Each tab retains its
scroll position. Probe internals and experimental raster/DXR integration are
collapsed by default. The window opens on the right with a bounded, resizable
size and uses the engine's existing ImGui colours.

The DXR settings audit follows the frame producer rather than the presence of
renderer resources:

| Setting | Full DXR behavior |
| --- | --- |
| Sun orientation, colour, intensity | Shared; disabled when the scene is sealed |
| Sun softness | Raster-only; DXR uses its fixed angular shadow sampling |
| Ambient tint/scale and world cubemap | Shared environment inputs, including GI |
| Reflection probe capture/box/interval | Raster-only; hidden and capture skipped in DXR |
| GI intensity, hysteresis, updates, volume and rays | Active; one indirect-lighting switch controls the master and DXR read flag |
| GI term/cascade/contact debug overlays | Raster-only; DXR uses its lighting-view diagnostic |
| CSM/local atlas/contact settings, SSR, SSAO, clustering | Raster-only; hidden in DXR |
| Material preview, room/Pillar materials and local lights | Shared |
| Preview light proxy shadow switch | Raster-only; DXR shadow rays always test local-light visibility |
| Texture filtering, TAA and specular AA | DXR controls; dependent sliders disabled when inactive |
| Height fog, volumetric sunlight and Post FX | Shared deferred/DXR controls |

The old full-frame DXR diagnostic switch duplicated the production renderer
switch and was overwritten by the frame update. It is removed from the UI;
lighting and temporal diagnostics live on the Debug tab. Experimental DXR sun
shadows/GI capture remain available for raster integration. Post FX parameters
are disabled when Post FX is off. Forward rendering does not run the shared
deferred atmosphere/post-FX passes, so those tabs are disabled in forward mode.

For layout captures, `BENCH_DEBUG_UI=1` displays the window in finite benchmarks
and `BENCH_DEBUG_TAB` selects a tab by its displayed name. These captures disable
ImGui ini saving so they do not overwrite the user's window layout.

Sponza normal cooking: main_sponza and the legacy sponza recipes declare
green-down DirectX source normals, so the cooker preserves their green
channel. main_sponza/ceiling_plaster_02 is the green-up exception and keeps
an explicit OpenGL override. Previously the wildcard OpenGL declaration
incorrectly inverted the other maps. Source slope-field cross derivatives
agree for the wall and floor maps (green-down) and have opposite signs for
the ceiling exception (green-up). This convention correction is separate
from preserving normal channels as linear data during WIC conversion.
The shader TBN convention remains unchanged across raster and DXR.

GameMain shares scene sun direction, color and intensity between raster,
DXR camera shading and inline GI capture. The .tgs lighting object supplies
sunPitch, sunYaw, sunIntensity, sunColor, ambientColor and authored lights.
Legacy bench_lights scene sidecars take priority when present. Switching to a
scene without lighting uses the default daylight values instead of inheriting
the previous scene's darkness. Sealed scenes receive no exterior sun or sky.

Use Render tuning > Sun / directional and Ambient / IBL for the shared inputs.
The DXR Renderer tab has independent contribution switches and Lighting view:
Beauty, Ambient occlusion, Environment, Diffuse GI, Albedo and Material AO /
roughness / metalness. AO modulates diffuse sky light and GI; specular
occlusion relaxes toward mirror-smooth surfaces. Direct light and emission
are not multiplied by ray AO. AO ray distance is in centimeters and uses
hit-distance falloff, rather than treating every nearby hit as fully black.

Reflections use one temporally varying GGX importance-sampled scene ray below
the reflection roughness cutoff. The ray carries its `BRDF * NdotL / PDF`
weight, so rough materials integrate a lobe instead of tracing only the mirror
direction. The prefiltered environment remains the fallback over the final 15%
of the cutoff, where one sample is too unstable before reflection denoising.
They are one-bounce scene rays with direct light, diffuse sky fill and GI;
the sky is not counted twice. HDR output is tone mapped and exposed by the
existing composite pass. Invalid lighting becomes magenta for diagnosis.
Camera, reflection, shadow and inline GI rays share a 0.33 alpha cutout test.
Materials explicitly assigned to the forward alpha-blend pass are ignored by
those rays, then composited over the resolved DXR HDR image by the existing
transparent pass. This prevents glass from becoming a false opaque DXR hit;
it is an intentional hybrid until material-authored transmission/refraction is
available.
The directional sun uses four rotated low-discrepancy samples over a solar
disk. The per-receiver rotation avoids the visible five-step penumbra produced
by the earlier fixed pattern, while camera jitter and temporal resolve
accumulate the resulting fine noise without doubling the shadow-ray budget.

The DXR GI probe capture follows the same split as a Lumen-style diffuse
transport cache. A ray miss stores mip-0 environment radiance in the probe's
SH coefficients, preserving the direction of sky visible through openings. A
geometry hit stores diffuse direct-light bounce, the low-frequency environment
bounce, and the decoded material emission. The final camera pass still owns
primary environment lighting, so the probe cache contributes only additional
indirect transport. Sealed scenes disable the environment source while
emissive geometry continues to illuminate the cache. This is intentionally a
single diffuse transport step per probe update; SH interpolation and the
existing hysteresis provide the temporal/spatial smoothing, not a full Lumen
surface cache or radiance-guided sampler.

Bistro specular maps retain G roughness and B metallic. Their zero red
channel is not valid AO; the cook recipe overrides AO with 1. The cooker
honors explicit channel overrides for packed ORM inputs.

Validation captures cover BistroExterior, PillarTest and the sealed built-in
room, including AO and GI switch comparisons. Benchmark switches are
BENCH_LIGHTING_VIEW (0..9), BENCH_DXR_AO and BENCH_DXR_GI.

Remaining limitations: no temporal AO/reflection denoiser, one GGX scene-ray
sample per pixel instead of a fully converged rough-reflection integral, a
single cached diffuse transport step per GI probe update, and cutout alpha
rather than glass refraction or transmission. Alpha candidates deliberately sample mip 0;
coverage-preserving alpha mips remain future work for distant foliage. Probe
placement/resolution still determines GI detail and can allow leakage in thin
or complex geometry. Scene-specific light rigs and HDR environments still
matter when matching a reference.

## Ray texture filtering

Ray texture filtering is enabled by default in DXR Renderer. Camera pixel
angular size defines the ray cone; reflections carry its accumulated width
and GI rays estimate their angular footprint from the probe sample count.
Triangle positions transformed into world space and triangle UV gradients
convert the cone footprint into explicit UV derivatives. SampleGrad uses a
16x anisotropic sampler for albedo, normal, ORM and emissive maps. This avoids
forcing all surfaces to mip 0 and retains more shallow-angle detail than a
single isotropic mip. Per-texture dimensions/mip counts are respected by the
sampler. Degenerate UVs and disabled filtering use a zero footprint.

Lighting view > Texture mip displays the estimated effective anisotropic
albedo mip (blue near 0, orange near 8). This is a footprint diagnostic rather
than the hardware's exact tap selection. BENCH_RAY_TEXTURE_FILTER=0 disables
filtering for comparisons. Changing the UI switch re-primes the GI volume.
Alpha candidate tests keep mip 0 to preserve the existing cutout threshold;
alpha coverage filtering remains separate work. Cone projection is capped at
very grazing angles. Curvature-aware reflected footprints and normal-variance
roughness compensation remain future improvements.

## Temporal input foundation

Sun rotation uses one convention across forward, deferred, DXR and shadow
passes: the directional light transform's forward axis points along sunlight
travel. Shading and rays toward the sun negate this axis; the cascade camera
looks along it. Positive pitch directs sunlight downward. Raster shaders use
GetSunDirectionToLight from Common.hlsli rather than interpreting forward as
a vector toward the sun. BENCH_DXR_RENDERER=0 selects raster for comparisons.

The full DXR camera pass writes render-resolution RG16F motion vectors,
R32F device depth, and RG32F metadata (motion-history validity in X,
expected previous device depth in Y). Each pixel is
written on both hit and miss paths. Depth is standard D3D [0,1], with sky at
1. Motion is previous pixel minus current pixel, measured in render pixels,
with positive Y down. Matrices and vectors currently contain no jitter.
Rigid objects use the previous rendered instance transform and the previous
camera projection; sky motion uses direction-only projection to omit camera
translation. Newly visible instances and the first frame have zero motion
and invalid history. Scene reload, resize, and switching through the raster
renderer reset camera history. Explicit camera cuts should call
DeferredRenderer::ResetTemporalHistory().

GetMotionVectorsSrv, GetTemporalDepthSrv, and GetMotionValiditySrv expose
these DXR inputs. They are usable after the DXR dispatch, before post effects
and UI. The command context transitions UAVs when subsequently read as SRVs.
Lighting view 7 displays signed XY motion around neutral 0.5 and history
validity in blue; view 8 displays inverse device depth. These views pass
through the usual exposure/tone mapping. Device depth is nonlinear and may
appear dark for distant objects.

## Specular anti-aliasing

The shared DXR hit decoder broadens GGX alpha squared using local normal
variance. Triangle edge/normal differences estimate interpolated geometric
normal variation across the ray footprint, including fixed materials.
Glossy textured hits take four additional filtered normal samples at quarter
footprint offsets to estimate normal-map variation. Roughness is increased
with r_new = (r^4 + min(2 * variance * strength, 0.1))^(1/4), clamped to 1.
Strength fades between roughness 0.4 and 0.7, and rough surfaces skip the
additional normal samples. Constant planar normals retain authored roughness.
The default strength is 0.25. Primary rays, reflection hits, and GI capture
use the same setting; raster materials are not adjusted by this DXR feature.

Render tuning exposes Specular anti-aliasing and its strength. Changes reset
TAA history and re-prime GI. Lighting view 9 displays added perceptual
roughness * 8 in red and resulting roughness in green. BENCH_SPECULAR_AA=0
disables it for comparisons. It uses the enabled ray-texture footprint;
disabling ray texture filtering disables the footprint-based adjustment.
The four samples are a bounded estimate, not a full precomputed normal
variance mip chain, and cannot recover detail already lost in coarse mips.

Rendering priorities after this stage: exponential height fog with shared
world-height/distance controls, then shadowed volumetric sunlight with
scattering and transmittance. DLSS integration is deferred until after these
atmosphere improvements. AO/reflection denoising remains a separate stage.

## Native DXR TAA

TAA is enabled by default in full DXR beauty rendering. A 16-sample Halton
sequence offsets camera rays in pixel space; motion matrices remain
unjittered. GetProjectionJitterPixels exposes the current sampling offset.
TemporalResolveCS runs between the DXR camera dispatch and HDR post effects,
using ping-pong RGBA16F color and R32F depth histories. Current color is
reconstructed onto the fixed output grid with UV minus current jitter.
Resolved history is already on that grid, so reprojection uses unjittered
pixel motion without adding the raw-frame jitter difference.
History is rejected outside the screen, when transforms are invalid, on
sky/geometry changes, and when linearized history depth differs from expected
previous surface depth by more than max(1 cm, 2 percent). Comparing against
the expected previous depth supports rigid object movement toward/away from
the camera. History color is clamped to the reconstructed current 3x3 YCoCg
bounds; motion reduces the configured history weight (default 0.9).
Jitter-induced luminance changes retain history rather than being classified
as reactive illumination. When camera and all rigid instance transforms and
the instance set are unchanged, depth differences represent jitter coverage
changes rather than disocclusion. This stationary mode preserves coverage
history without depth/color clipping and uses a separate stationary weight
(default 0.95). During movement, depth matching searches the contributing
2x2 history footprint (samples with bilinear weight above 0.05) and color
clipping uses raw current neighborhood extrema to retain thin coverage.
Motion and invalid/new history disable the stationary shortcut. This must
not be used for deforming geometry without a matching motion-state signal.

Scene reload, resize, changing projection, large camera jumps/rotations,
switching render pipelines, and changing TAA activation invalidate history.
Lighting diagnostic views bypass TAA and jitter. Render tuning exposes
the toggle, history weight, manual reset, reprojected history (magenta for
invalid history), and rejection mask (red rejected, green accepted). Debug
display never enters color history. BENCH_TAA=0 disables TAA,
BENCH_TAA_VIEW=1/2 selects its diagnostics, and BENCH_TAA_RESET_FRAME requests
a reset at a benchmark frame for validation.

This is native-resolution DXR TAA, not DLSS or a dedicated ray denoiser.
Raster needs motion generation before sharing the resolve. Skinned/deforming
meshes require previous vertex positions; moving reflections may need separate
motion/hit-distance inputs. Bilinear history filtering and conservative
neighborhood bounds can soften fine detail, and depth/color checks do not
fully detect moving reflections or animated illumination.
# Height fog and volumetric sunlight

The deferred raster and full DXR renderers share `AtmosphereParams` (176 bytes,
b8). Height fog integrates exponential density analytically along the camera
ray and composites scene-linear HDR as `radiance * transmittance + fogColour *
(1 - transmittance) + sunlight`. World positions remain in engine centimeters;
density, base height, height falloff and distance controls use meters. The
near-zero falloff limit avoids a division at horizontal rays. Optical depth and
exponents are bounded for deep downward views. Density zero bypasses the beauty
pass entirely.

Sunlight uses fixed midpoint quadrature at half resolution, normalized
Henyey–Greenstein forward scattering, and depth-aware bilinear upsampling. DXR
visibility shares the scene's material alpha test and stops at the first accepted
occluder. Raster uses the current shadow cascades. Raster sunlight is suppressed
outside available shadow coverage or with shadows disabled, avoiding unshadowed
light leaks. A zero-intensity sun skips volumetric dispatch.

In DXR, atmosphere is applied to jittered ray radiance **before** temporal resolve;
TAA therefore accumulates both surface and atmospheric samples together. In
raster it follows the transparent pass. Both paths run before bloom/exposure,
independently of the Post FX enable switch. Lighting and TAA diagnostics bypass
atmosphere; atmosphere diagnostics bypass temporal blending. UI atmosphere
changes reset TAA history. There is no separate volumetric temporal history or
random sampling.

`Render tuning > Atmosphere` exposes enable switches, extinction density, height
falloff/base height, start/range, linear fog colour, sky participation, sunlight
strength, phase anisotropy, range and step count. Defaults are intentionally
subtle: density 0.0015/m, falloff 0.025/m, start 5m, sunlight strength 0.5, 24 steps
and 120m sunlight range. Sky fog is opt-in. Controls apply across scene switches.

This first version models a global height medium with a constant ambient fog
colour and single scattering of directional sunlight. It does not yet include
local fog volumes, point/spot volumetrics, light-path atmospheric extinction,
transparent depth layers, or multiple scattering. Raster cascade coverage and
half-resolution sampling can limit distant or very thin beams. DLSS is deferred.

Validation hooks: `BENCH_FOG`, `BENCH_FOG_DENSITY`, `BENCH_VOLUMETRIC`,
`BENCH_ATMOSPHERE_VIEW` (0 beauty, 1 transmittance, 2 scattered sunlight), and
`BENCH_SUN_INTENSITY`. Use the diagnostics with manual exposure and bloom disabled
when comparing raw pass behavior.
