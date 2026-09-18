// Shared bindless-scene declarations + direct-lighting helpers for every DXR
// compute pass in the engine (DxrLightingCS.hlsl, GiTraceInlineCS.hlsl).
// Kept as one header so the TLAS/material/light bindings and the BRDF can't
// drift apart between passes the way they briefly did across DxrLightingCS's
// own iterations. See Dx12Device::kRayTlasRootParameter and friends for what
// binds each of these on the C++ side.
#ifndef DXR_COMMON_HLSLI
#define DXR_COMMON_HLSLI

#include "GiCommon.hlsli"

// Keep the DXR material decoder independent of Common.hlsli: that shared
// raster header declares its own fixed-register resources, which would clash
// with this bindless RayQuery layout. This is the same _FX normalization used
// by GBufferPS.
#ifndef MAX_EMISSIVE_STRENGTH
#define MAX_EMISSIVE_STRENGTH 16.0f
#endif

// --- GI reconstruction switches (see EvaluateDxrGi) -----------------------
// These shaders are compiled from source at runtime, so flipping any of these
// is a file edit and a restart, not a rebuild -- which makes them the A/B
// levers for the two changes they control.
//
// DXR_GI_DIRECTIONAL: 0 reproduces the old L0-only lookup (flat, no colour
// bleed). 1 reconstructs the full L1/L2 irradiance, which is what carries
// directional bounce -- i.e. colour bleed.
#ifndef DXR_GI_DIRECTIONAL
#define DXR_GI_DIRECTIONAL 1
#endif
#ifndef DXR_GI_L1_WEIGHT
#define DXR_GI_L1_WEIGHT 0.85f
#endif
#ifndef DXR_GI_L2_WEIGHT
#define DXR_GI_L2_WEIGHT 0.5f
#endif
// DXR_GI_VISIBILITY: 0 ignores the probes' stored distance moments (light
// leaks through walls). 1 weights each probe by its own Chebyshev visibility
// toward the shaded point.
#ifndef DXR_GI_VISIBILITY
#define DXR_GI_VISIBILITY 1
#endif
// DXR_TWO_SIDED_SHADOWS: 0 reproduces the old shadow ray -- back-face culled,
// and blind to every triangle sharing the receiver's instance. 1 makes
// occlusion two-sided and rejects only the originating triangle. See
// TraceShadowRay.
// DXR_REFLECTION_SUN_SAMPLES: sun-shadow rays per reflection hit (primary hits
// always use 4). See TraceReflection.
#ifndef DXR_REFLECTION_SUN_SAMPLES
#define DXR_REFLECTION_SUN_SAMPLES 1u
#endif
// DXR_SELF_SHADOW_DISTANCE: world units (cm) within which a shadow ray ignores
// triangles of the instance it started on. See TraceShadowRay.
// DXR_SHADOW_TERMINATOR_OFFSET: lift shadow-ray origins onto the smooth surface
// implied by vertex normals (see DecodeHit). 0 = start from the flat facet.
#ifndef DXR_SHADOW_TERMINATOR_OFFSET
#define DXR_SHADOW_TERMINATOR_OFFSET 1
#endif
#ifndef DXR_SELF_SHADOW_DISTANCE
#define DXR_SELF_SHADOW_DISTANCE 10.0f
#endif
#ifndef DXR_TWO_SIDED_SHADOWS
#define DXR_TWO_SIDED_SHADOWS 1
#endif


RaytracingAccelerationStructure gScene : register(t0, space2);

#include "MaterialParams.hlsli"

struct RayGeometryLookup
{
	uint vertexSrv;
	uint indexSrv;
	uint materialIndex;
	uint vertexStride;
	uint positionOffset;
	uint normalOffset;
	uint uv0Offset;
	uint tangentOffset;
	uint binormalOffset;
	uint vertexFormat;           // 0 full Vertex, 1 compact MeshVertex
	uint indexCount;             // lets a compute pass walk this instance's triangles
	uint _pad2;
	float4 previousTransform0, previousTransform1, previousTransform2;
	uint motionHistoryValid;
	uint3 _motionPad;
	// Current world transform (row-major 3x4), so emissive geometry can be
	// placed in world space without going through the TLAS.
	float4 transform0, transform1, transform2;
};

// snorm16 stored in the low 16 bits of v.
float SnormFromU16(uint v)
{
	const int s = int(v << 16) >> 16;
	return max(float(s) / 32767.0f, -1.0f);
}

float3 MeshOctDecode(float2 e)
{
	float3 n = float3(e.x, e.y, 1.0f - abs(e.x) - abs(e.y));
	const float t = saturate(-n.z);
	n.xy -= (step(0.0f, n.xy) * 2.0f - 1.0f) * t;   // no vector ternary: DXC HLSL 2021 rejects it
	return normalize(n);
}
StructuredBuffer<RayGeometryLookup> gRayGeometry : register(t1, space2);

// Per-mesh raw vertex/index descriptor table BuildRaytracingTlas's caller
// registers each instance's SRVs into (Dx12Device's rayGeometryRange, space1,
// kRaySceneDescriptorCapacity descriptors) -- gRayGeometry[instanceId] gives
// the two indices into this array to use for that instance's geometry.
ByteAddressBuffer gRawGeometry[32768] : register(t0, space1);

// The identical bindless table as gRawGeometry above, re-declared as
// Texture2D under space4 (Dx12Device::kRaySceneTexRootParameter) so texture
// SRVs registered into the same table (RayTracingMaterialTable's
// albedo/normal/orm/emissive) can actually be sampled.
Texture2D gRaySceneTex[32768] : register(t0, space4);
SamplerState gMaterialSampler : register(s0);
TextureCube gDxrEnvironment : register(t3);

// Per-material GPU record (RayTracingMaterialTable::GpuRecord), indexed by
// RayGeometryLookup::materialIndex. A normal per-dispatch SRV (space0, not
// the fixed space2 pair) since this is engine-owned, CPU-updated state that
// changes only when new materials are discovered, not every frame.
struct RayMaterialRecord
{
	uint albedoSrv;
	uint normalSrv;
	uint ormSrv;
	uint emissiveSrv;
	MaterialParams params;
	uint rayVisibility;   // RayTracingMaterialTable::kRay*: 1 transparent (skipped), 3 opaque
	uint3 _recordPad;
};
StructuredBuffer<RayMaterialRecord> gMaterials : register(t0);

// Emissive geometry as area lights, built by EmissiveLightGatherCS. Art like
// Bistro has no punctual lights at all, so without this the lamps and signs are
// visible but illuminate nothing.
struct EmissiveLight
{
	float3 p0;   float area;
	float3 e0;   uint  instanceId;
	float3 e1;   uint  primitiveIndex;
	float3 radiance;
	float  power;
};
StructuredBuffer<EmissiveLight> gEmissiveLights : register(t7);
StructuredBuffer<uint> gEmissiveLightCount : register(t8);

// A ReSTIR reservoir: the one light sample this pixel kept, plus the weight that
// makes it an unbiased estimate of the whole list, and how many candidates it
// stands for. Stored per pixel and reprojected next frame, which is what turns
// one shadow ray per frame into an effective sample count in the hundreds.
struct LightReservoir
{
	float3 lightPoint;   float W;        // unbiased contribution weight
	float3 radiance;     uint  M;        // candidates this reservoir represents
	float3 lightNormal;  uint  packedSurfaceNormal;   // octahedral, 2 x f16: the surface this reservoir was built for
	float3 surfacePos;   float _pad;
};

uint PackReservoirNormal(float3 n)
{
	n /= max(abs(n.x) + abs(n.y) + abs(n.z), 1e-6f);
	const float2 sgn = float2(n.x >= 0.0f ? 1.0f : -1.0f, n.y >= 0.0f ? 1.0f : -1.0f);
	float2 e = n.z >= 0.0f ? n.xy : (1.0f - abs(n.yx)) * sgn;
	return f32tof16(e.x) | (f32tof16(e.y) << 16);
}
float3 UnpackReservoirNormal(uint p)
{
	float2 e = float2(f16tof32(p & 0xFFFFu), f16tof32(p >> 16));
	float3 n = float3(e, 1.0f - abs(e.x) - abs(e.y));
	if (n.z < 0.0f) n.xy = (1.0f - abs(n.yx)) * float2(n.x >= 0.0f ? 1.0f : -1.0f, n.y >= 0.0f ? 1.0f : -1.0f);
	return normalize(n);
}

// A neighbour's reservoir is only reusable if it was built for a surface that
// could plausibly see the same lights: same-facing, and on the same plane.
bool ReservoirSurfaceMatches(LightReservoir r, float3 pos, float3 n)
{
	const float3 rn = UnpackReservoirNormal(r.packedSurfaceNormal);
	if (dot(rn, n) < 0.9f) return false;
	const float3 d = r.surfacePos - pos;
	const float dist = max(length(pos - r.surfacePos), 1.0f);
	return abs(dot(d, n)) < 0.02f * dist + 2.0f;   // plane distance, cm: 2% of separation + 2 cm
}
StructuredBuffer<LightReservoir> gPrevReservoirs : register(t9);
RWStructuredBuffer<LightReservoir> gReservoirs : register(u12);

// DeferredRenderer's own point/spot light list -- same layout as GpuLight in
// DeferredLightingPS.hlsl (kept in sync manually; the raster and DXR paths
// don't share a register layout, only this struct's shape).
struct GpuLight
{
	float3 position;
	float  range;
	float3 color;
	float  radius;
	float3 spotDir;
	float  spotCosOuter;
	float  spotCosInner;
	float  shadowSlot;
	float2 _lpad;
};
StructuredBuffer<GpuLight> gLights : register(t1);

// Same storage/layout as DeferredLightingPS's irradiance volume. Only the
// primary DXR shading pass binds this SRV; the RT probe writer uses the UAV.
StructuredBuffer<float4> gGiSH : register(t2);
StructuredBuffer<float4> gGiVisibility : register(t5); // 8x8 oct distance moments per probe
cbuffer DxrGiVolumeBuffer : register(b13)
{
	float4 gDxrGiOrigin;
	float4 gDxrGiSpacing;
	int4   gDxrGiCounts;
};

// Standard HSV->RGB (6-way piecewise) -- used by debug-only fallback colors.
float3 HsvToRgb(float3 hsv)
{
	const float3 k = float3(1.f, 2.f / 3.f, 1.f / 3.f);
	const float3 p = abs(frac(hsv.x + k) * 6.f - 3.f);
	return hsv.z * lerp(k.xxx, saturate(p - 1.f), hsv.y);
}

// Standard direct-lighting Cook-Torrance: GGX distribution, Smith
// (Schlick-GGX) geometry term, Schlick Fresnel.
float3 CookTorrance(float3 n, float3 v, float3 l, float3 albedo, float roughness, float metalness, out float3 outKd)
{
	const float3 h = normalize(v + l);
	const float ndotv = max(dot(n, v), 1e-4f);
	const float ndotl = max(dot(n, l), 1e-4f);
	const float ndoth = max(dot(n, h), 0.f);
	const float vdoth = max(dot(v, h), 0.f);

	const float alpha = max(roughness * roughness, 1e-3f);
	const float alpha2 = alpha * alpha;
	const float d = alpha2 / (3.14159265f * pow(ndoth * ndoth * (alpha2 - 1.f) + 1.f, 2.f));

	const float k = pow(roughness + 1.f, 2.f) / 8.f;
	const float g1v = ndotv / (ndotv * (1.f - k) + k);
	const float g1l = ndotl / (ndotl * (1.f - k) + k);
	const float g = g1v * g1l;

	const float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metalness);
	const float3 f = f0 + (1.f - f0) * pow(1.f - vdoth, 5.f);

	const float3 specular = (d * g * f) / max(4.f * ndotv * ndotl, 1e-4f);
	outKd = (1.f - f) * (1.f - metalness);
	return specular;
}

// Candidate alpha testing is shared by camera, reflection, shadow and GI rays.
//
// Which candidates reach this function is now decided per TLAS instance, not
// per ray: BuildRaytracingTlas marks an instance FORCE_OPAQUE when
// RayTracingMaterialTable::IsRayOpaque proves this function could only ever
// return true for it, and FORCE_NON_OPAQUE otherwise. Camera, reflection, GI
// and fog rays therefore no longer carry RAY_FLAG_FORCE_NON_OPAQUE and let
// traversal hardware commit ordinary opaque geometry without ever exiting to
// the shader. Keep IsRayOpaque and the early-outs below in lockstep.
//
// TraceAmbientOcclusion is the remaining exception: it rejects
// same-instance candidates (TraceShadowRay no longer does -- see its own note),
// which only works while candidates still reach the shader, so it
// keeps the ray flag and still runs every candidate through here.
// Match rayVisibility (RayTracingMaterialTable::FixedMaterial): 0 =
// Unclassified (no one has confirmed this material has no cutout, so fall
// back to sampling -- the safe default for any loader that never calls
// SetRayVisibility), 1 = Transparent (forward-only, never a DXR hit), 2 =
// Masked (confirmed real per-pixel cutout, matches raster's 0.33 threshold),
// 3 = Opaque (confirmed no cutout -- skip the texture sample entirely; this
// is the actual performance fast path, since Unclassified and Masked both
// still sample below).
// mipBias: alpha-tested (masked) geometry -- foliage, fences -- forces every
// candidate triangle along a ray through this any-hit test, and a ray dense
// with overlapping leaves/branches can invoke it many times. Boolean
// occlusion rays (shadow, AO) fire several samples per pixel and only need
// "roughly the right silhouette", not a pixel-exact cutout edge, so callers
// that are purely stochastic occlusion tests pass a positive bias to sample
// a cheaper, smaller mip instead of always forcing mip 0. Primary/reflection/
// GI callers leave this at the default 0 so directly visible detail keeps
// its exact authored edge.
bool AcceptRayTriangle(uint instanceId, uint primitiveIndex, float2 bary, float mipBias = 0.0f)
{
	const RayGeometryLookup g = gRayGeometry[instanceId];
	const RayMaterialRecord mat = gMaterials[g.materialIndex];
	if (mat.rayVisibility == 1u) return false;
	if (mat.rayVisibility == 3u) return true;
	if ((mat.params.flags & MATERIAL_FLAG_USE_TEXTURES) == 0u || mat.albedoSrv == 0u) return true;
	const uint3 tri = gRawGeometry[NonUniformResourceIndex(g.indexSrv)].Load3(primitiveIndex * 12u);
	float2 uv[3];
	[unroll] for (uint i = 0; i < 3; ++i)
		uv[i] = asfloat(gRawGeometry[NonUniformResourceIndex(g.vertexSrv)].Load2(tri[i] * g.vertexStride + g.uv0Offset));
	const float2 hitUv = uv[0] * (1.0f - bary.x - bary.y) + uv[1] * bary.x + uv[2] * bary.y;
	return gRaySceneTex[NonUniformResourceIndex(mat.albedoSrv)].SampleLevel(gMaterialSampler, hitUv, mipBias).a * mat.params.opacity >= mat.params.alphaCutoff;
}

// 1 = unoccluded, 0 = occluded. maxDist should stop just short of the light
// itself (dist - a small epsilon) so a light's own bulb mesh doesn't shadow
// itself out.
float TraceShadowRay(float3 origin, float3 dir, float maxDist, uint sourceInstanceId, uint sourcePrimitive)
{
	RayDesc r;
	r.Origin = origin;
	r.Direction = dir;
	// Scene positions are centimetres.  0.01 (0.1 mm) was smaller than the
	// reconstruction/traversal error on several imported architectural meshes,
	// so neighbouring triangles could immediately occlude their own receiver.
	// A 0.5 mm minimum is still far below visible contact detail. Same-instance
	// candidates are also ignored below: thin, single-sided cloth and foliage
	// meshes commonly fold over themselves, and treating their neighbouring
	// triangles as opaque blockers creates hard triangle-shaped shadow acne.
	r.TMin = 0.05f;
	r.TMax = maxDist;
	if (r.TMax <= r.TMin) return 1.0f;
	// Two independent light leaks used to live in this query.
	//
	// 1. RAY_FLAG_CULL_BACK_FACING_TRIANGLES. Occlusion is not a two-sided
	//    question of taste: if geometry is between the surface and the light,
	//    it blocks, whichever way its triangles happen to wind. Culling back
	//    faces let the sun pass straight through every single-sided wall
	//    approached from its reverse side.
	// 2. Rejecting EVERY candidate sharing the receiver's instance. This was
	//    guarding against self-shadowing acne on thin folded cloth, but
	//    ModelFactory merges sub-meshes by material, so one "instance" is a
	//    large share of the whole scene -- meaning a wall could not shadow a
	//    floor that happened to use the same material. That is the big one.
	//
	// The replacement rejects only the originating triangle, which is all the
	// acne guard ever actually needed: the ray origin is already pushed off the
	// surface along the geometric normal by OffsetRayOrigin /
	// ReconstructRaySurface, so neighbouring triangles cannot self-intersect
	// unless that offset is too small, and the fix for that is the offset, not
	// blinding the ray to a quarter of the scene.
#if DXR_TWO_SIDED_SHADOWS
	RayQuery<RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> sq;
#else
	RayQuery<RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> sq;
#endif
	sq.TraceRayInline(gScene, RAY_FLAG_NONE, 0xFF, r);
	// A RayQuery is a traversal state machine.  One Proceed() only advances to
	// the next candidate; the committed hit is final only after traversal ends.
	while (sq.Proceed()) {
#if DXR_TWO_SIDED_SHADOWS
		// Rejecting only the origin triangle was not enough: on thin, low-poly
		// folded cloth (Sponza's swags) the neighbouring triangles of the same
		// mesh sit a few centimetres away and occlude the sun wherever the flat
		// facets disagree with the smooth shading normal -- the "shadow
		// terminator" problem -- which rendered sunlit swags as black blotches
		// with jagged lit triangles. So same-instance occluders are ignored
		// within DXR_SELF_SHADOW_DISTANCE as well. Beyond it they still cast
		// shadows, which is what keeps the light-leak fix: the old code ignored
		// the whole (material-merged, scene-spanning) instance at any distance.
		// A same-mesh BACK face means the ray is leaving through the receiver's
		// own sheet -- double-layered or slightly interpenetrating thin cloth
		// (Sponza's swags are both). A thin sheet cannot shadow itself that way,
		// so those are ignored. Same-mesh FRONT faces still occlude, and so does
		// everything belonging to other meshes, from either side.
		const bool isSelf = sq.CandidateInstanceID() == sourceInstanceId &&
			(sq.CandidatePrimitiveIndex() == sourcePrimitive ||
			 !sq.CandidateTriangleFrontFace() ||
			 sq.CandidateTriangleRayT() < DXR_SELF_SHADOW_DISTANCE);
#else
		const bool isSelf = sq.CandidateInstanceID() == sourceInstanceId;
#endif
		if (sq.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE && !isSelf &&
			AcceptRayTriangle(sq.CandidateInstanceID(), sq.CandidatePrimitiveIndex(), sq.CandidateTriangleBarycentrics(), 2.0f))
			sq.CommitNonOpaqueTriangleHit();
	}
	return (sq.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? 0.f : 1.f;
}

float3 OffsetRayOrigin(float3 position, float3 geometricNormal, float3 outgoingDirection)
{
	const float3 n = normalize(geometricNormal);
	return position + n * (dot(n, outgoingDirection) >= 0.0f ? 0.05f : -0.05f);
}

// Integer hash of a world position's bit pattern. Varies per pixel (no two
// shading points share a position) but is stable for a given surface point
// across frames, so the temporal resolve keeps seeing the same estimator for
// the same point rather than a pattern that swims with the camera.
uint HashPosition(float3 position, uint salt)
{
	uint h = asuint(position.x) * 73856093u ^ asuint(position.y) * 19349663u ^ asuint(position.z) * 83492791u;
	h += salt * 747796405u;
	h ^= h >> 16; h *= 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16;
	return h;
}
float UnitFromHash(uint h) { return (float)(h & 0x00ffffffu) / 16777216.0f; }

float TraceSunVisibility(float3 position, float3 geometricNormal, float3 dir, uint sourceInstanceId, uint sourcePrimitive, uint sampleCount = 4u, float rotationOffset = 0.0f)
{
	const float3 t = normalize(abs(dir.y) < 0.99f ? cross(dir, float3(0,1,0)) : cross(dir, float3(1,0,0)));
	const float3 b = cross(dir, t);
	// The sun disc is sampled in polar coordinates, so it needs TWO random
	// numbers, not one. The previous version randomised only the azimuth and
	// took the radius straight from the stratum centre, sqrt((i+0.5)/N) -- at
	// one sample per pixel that is a constant sqrt(0.5), i.e. every pixel in
	// the scene sampled the same ring of the disc and none sampled its centre
	// or its rim. The penumbra that produced was both biased and identical
	// everywhere, which no amount of temporal or spatial filtering can fix,
	// because there is no variance for a filter to average away.
	//
	// Both coordinates now get their own decorrelated per-point value, and
	// both are rotated per frame (Cranley-Patterson), so one ray per pixel is
	// an unbiased estimate that the denoiser and the temporal resolve can
	// actually converge. The radial stratum is kept for the multi-sample
	// counts, where it still helps.
	const uint h = HashPosition(position, 0u);
	const float rotAngle = frac(UnitFromHash(h) + rotationOffset);
	const float rotRadius = frac(UnitFromHash(HashPosition(position, 0x9e3779b9u)) + rotationOffset * 0.7548777f);
	float visibility = 0.0f;
	const uint sunSamples = clamp(sampleCount, 1u, 4u);
	const float invSunSamples = 1.0f / float(sunSamples);
	[loop] for (uint i = 0; i < sunSamples; ++i) {
		const float u = frac((float(i) + 0.5f) * invSunSamples + rotRadius);
		uint bits = i;
		bits = (bits << 16u) | (bits >> 16u);
		bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
		bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
		bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
		bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
		const float v = float(bits) * 2.3283064365386963e-10f;
		const float angle = 6.283185307f * frac(v + rotAngle);
		const float radius = sqrt(u);
		const float3 sampleDir = normalize(dir + 0.00329f * radius * (t * cos(angle) + b * sin(angle)));
		visibility += TraceShadowRay(OffsetRayOrigin(position, geometricNormal, sampleDir), sampleDir, 100000.0f, sourceInstanceId, sourcePrimitive);
	}
	return visibility * invSunSamples;
}

// Windowed inverse-square falloff, in METERS -- matches PBRFunctions.hlsli's
// EvaluatePointLight/EvaluateSpotLight exactly: `dist`/`range` arrive in the
// engine's raw world units (centimeters), and inverse-square on that number
// directly is ~10,000x too small versus what the authored light colors assume.
float PunctualAttenuation(float distWorldUnits, float rangeWorldUnits)
{
	const float distM = 0.01f * distWorldUnits;
	const float rangeM = max(0.01f * rangeWorldUnits, 1e-4f);
	const float distSq = max(distM * distM, 1e-4f);
	const float win = saturate(1.f - pow(distM / rangeM, 4.f));
	return (win * win) / distSq;
}

// The game represents emissive spheres with a non-zero light radius. Treating
// those records as mathematical points makes inverse-square attenuation and
// the GGX lobe singular near the source, producing isolated white "light
// dots" on otherwise rough diffuse receivers. This is a stable sphere-light
// approximation: use the nearest finite source distance and broaden the
// specular lobe by its subtended angle.
float AreaLightDistance(float centerDistance, float radius)
{
	if (radius <= 0.0f) return centerDistance;
	return max(centerDistance - radius, max(radius * 0.5f, 0.01f));
}

// Authoring point lights use radius == 0, but a delta emitter is not a stable
// real-time shading primitive: it produces a one-pixel GGX singularity on a
// rough plane. Treat it as a small finite bulb in the DXR path. Explicit area
// lights keep their authored radius.
float EffectiveLightRadius(float authoredRadius)
{
	return max(authoredRadius, 20.0f); // 20 cm in this renderer's world units
}

// Direct contribution of one point/spot light, including its own shadow ray.
float3 EvaluatePunctualLight(GpuLight L, float3 surfacePosition, float3 geoNormal, uint sourceInstanceId, uint sourcePrimitive, float3 n, float3 v,
                              float3 albedo, float roughness, float metalness)
{
	const float3 toLight = L.position - surfacePosition;
	const float dist = length(toLight);
	if (dist >= L.range || dist < 1e-4f) return 0.f;
	const float3 l = toLight / dist;

	float spotFactor = 1.f;
	if (L.spotCosOuter > 0.f)
	{
		const float cosAngle = dot(L.spotDir, -l);
		spotFactor = smoothstep(L.spotCosOuter, L.spotCosInner, cosAngle);
		if (spotFactor <= 0.f) return 0.f;
	}

	const float ndotl = saturate(dot(n, l));
	if (ndotl <= 0.f) return 0.f;

	const float sourceRadius = EffectiveLightRadius(L.radius);
	const float lightDistance = AreaLightDistance(dist, sourceRadius);
	const float atten = PunctualAttenuation(lightDistance, L.range) * spotFactor;
	if (atten <= 1e-5f) return 0.f;

	const float3 lightOrigin = surfacePosition + geoNormal * (dot(geoNormal, l) >= 0.0f ? 0.05f : -0.05f);
	const float shadow = TraceShadowRay(lightOrigin, l, dist - 0.5f, sourceInstanceId, sourcePrimitive);
	if (shadow <= 0.f) return 0.f;

	float3 kd;
	const float angularRadius = saturate(sourceRadius / max(dist, sourceRadius));
	const float filteredRoughness = sqrt(roughness * roughness + angularRadius * angularRadius);
	const float3 specular = CookTorrance(n, v, l, albedo, filteredRoughness, metalness, kd);
	const float3 diffuse = kd * albedo / 3.14159265f;
	return (diffuse + specular) * L.color * (ndotl * atten * shadow);
}

// Real environment radiance shared by DXR reflections and DXR GI. A negative
// mip is the explicit "no environment" representation for sealed scenes.
//
// Matches SkyboxPS.hlsl exactly (mip 0, intensity 1, no invented exposure
// scale or clamp) -- the raster skybox samples the cube raw and trusts the
// existing HDR auto-exposure + ACES tonemap pass (Composite) to compress it,
// and the DXR path shares that exact same Composite pass (see
// ResolveDxrLightingToHdr -> myHdr -> Composite), so it must feed it the same
// kind of un-pre-exposed value. The earlier *0.025 + clamp(4) fudge here
// pre-baked its own guessed exposure on top of the real one, and `tint` was
// the GI-tuned kDxrEnvironmentScale (0.12) rather than 1 -- together making
// the DXR sky/reflections come out far dimmer and differently-toned than the
// raster skybox and any real HDRI.
float3 SkyRadiance(float3 dir, float mip, float3 tint)
{
	if (mip < 0.0f) return 0.0f;
	return gDxrEnvironment.SampleLevel(gMaterialSampler, dir, mip).rgb * tint;
}

float GiMomentVisibility(float4 moments, float distance)
{
	if (moments.z < 0.5f || distance <= moments.x) return 1.0f;
	const float variance = max(moments.y - moments.x * moments.x, 0.01f);
	const float delta = distance - moments.x;
	return max(variance / (variance + delta * delta), 0.02f);
}

// Bilinear filtering avoids the cell boundaries produced by selecting one of
// the probe's 8x8 octahedral moment bins. The direction is from the probe to
// the shaded surface, matching the direction stored by GiTraceInlineCS.
float GiProbeVisibility(uint probe, float3 probeToSurface, float distance)
{
	float2 oct = probeToSurface.xy / max(abs(probeToSurface.x) + abs(probeToSurface.y) + abs(probeToSurface.z), 1e-5f);
	if (probeToSurface.z < 0.0f)
		oct = (1.0f - abs(oct.yx)) * sign(oct.xy + 1e-6f);
	const float2 texel = oct * 4.0f + 3.5f;
	const int2 base = clamp((int2)floor(texel), int2(0,0), int2(6,6));
	const float2 fraction = saturate(texel - float2(base));
	float visibility = 0.0f;
	[unroll] for (int y = 0; y < 2; ++y) [unroll] for (int x = 0; x < 2; ++x)
	{
		const float wx = x == 0 ? 1.0f - fraction.x : fraction.x;
		const float wy = y == 0 ? 1.0f - fraction.y : fraction.y;
		const uint bin = uint(base.x + x + (base.y + y) * 8);
		visibility += GiMomentVisibility(gGiVisibility[probe * 64u + bin], distance) * wx * wy;
	}
	return visibility;
}

// Trilinear L2-SH volume lookup with DDGI-style directional visibility.
float3 EvaluateDxrGi(float3 worldPos, float3 n)
{
	if (gDxrGiOrigin.w < 0.5f || any(gDxrGiCounts.xyz < 2)) return 0.0f;
	const float3 counts = float3(gDxrGiCounts.xyz);
	const float3 raw = (worldPos - gDxrGiOrigin.xyz) /
		max(gDxrGiSpacing.xyz, float3(1e-4f, 1e-4f, 1e-4f));
	const float3 grid = clamp(raw, 0.0f, counts - 1.001f);
	const float outside = length(max(0.0f, abs(raw - (counts - 1.0f) * 0.5f) - (counts - 1.0f) * 0.5f));
	const float fade = saturate(1.0f - outside);
	if (fade <= 0.0f) return 0.0f;

	const int3 baseCell = clamp((int3)floor(grid), 0, gDxrGiCounts.xyz - 2);
	const float3 fraction = saturate(grid - (float3)baseCell);

	// Shift the lookup off the surface before computing probe weights and
	// visibility. Without it a receiver sits exactly on the occluder that the
	// probes recorded, so its own wall reads as "something is in the way" and
	// the surface self-occludes its own bounce. Scaled by cell size so it
	// behaves the same on any volume density.
	const float3 biasedPos = worldPos + n * (0.25f * min(gDxrGiSpacing.x, min(gDxrGiSpacing.y, gDxrGiSpacing.z)));

	// Blend in COEFFICIENT space and reconstruct once at the end, rather than
	// reconstructing irradiance at each of the 8 corners and blending the
	// results. Blending reconstructed values is direction-blind -- it averages
	// eight different directional answers -- and that is what produced the
	// triangular faceting on flat receivers which caused the directional bands
	// to be rolled back to L0-only previously. Blending the coefficients is
	// linear and exact, because SH reconstruction is itself linear in them.
	float3 shSum[9];
	[unroll] for (uint c = 0; c < 9; ++c) shSum[c] = 0.0f;
	float totalWeight = 1e-4f;
	[unroll] for (int o = 0; o < 8; ++o)
	{
		const int3 offset = int3(o & 1, (o >> 1) & 1, (o >> 2) & 1);
		const int3 cell = baseCell + offset;
		const float3 axisWeight = lerp(1.0f - fraction, fraction, (float3)offset);
		float weight = axisWeight.x * axisWeight.y * axisWeight.z;
		const float3 probePos = gDxrGiOrigin.xyz + (float3)cell * gDxrGiSpacing.xyz;
		const uint probe = cell.x + cell.y * gDxrGiCounts.x + cell.z * gDxrGiCounts.x * gDxrGiCounts.y;
		// GiSH slot 8.w is written only after a probe has received its first
		// measurement. Never blend a cleared black probe into a valid cell.
		if (gGiSH[probe * 9u + 8u].w < 0.5f) continue;
		const float3 toProbe = probePos - biasedPos;
		const float d = length(toProbe);
		if (d > 1e-3f)
		{
			// Backface weight: a probe behind the receiver contributed nothing
			// that could legitimately reach its front.
			weight *= saturate(dot(n, toProbe / d) * 0.5f + 0.5f);
#if DXR_GI_VISIBILITY
			// Chebyshev occlusion from the probe's own 8x8 octahedral distance
			// moments -- these have been written by GiTraceInlineCS all along and
			// simply never read. This is what stops a probe on the far side of a
			// wall from lighting this surface through it. The moments are indexed
			// by the probe-to-surface direction, matching how they were stored.
			//
			// The earlier attempt applied this raw and produced circular pools on
			// flat surfaces; the difference here is the normal bias above (so a
			// surface no longer occludes itself) and the floor below (so a probe
			// is attenuated, never hard-zeroed, which is what turned the falloff
			// into a visible radial kernel).
			weight *= max(GiProbeVisibility(probe, -toProbe / d, d), 0.08f);
#endif
		}

		float3 sh[9];
		[unroll] for (uint i = 0; i < 9; ++i) sh[i] = gGiSH[probe * 9u + i].rgb;
		[unroll] for (uint j = 0; j < 9; ++j) shSum[j] += sh[j] * weight;
		totalWeight += weight;
	}

	const float invWeight = 1.0f / totalWeight;
	[unroll] for (uint k = 0; k < 9; ++k) shSum[k] *= invWeight;

#if DXR_GI_DIRECTIONAL
	// Damp the higher bands rather than trusting them fully. A probe traced with
	// a few hundred rays has real but noisy L1/L2 content, and undamped L2 rings
	// (over/undershoots) on high-contrast transitions. L1 is what actually
	// carries colour bleed -- the directional "this wall is red" signal -- so it
	// keeps most of its energy; L2 is mostly shape refinement and is halved.
	[unroll] for (uint m = 1; m < 4; ++m) shSum[m] *= DXR_GI_L1_WEIGHT;
	[unroll] for (uint p = 4; p < 9; ++p) shSum[p] *= DXR_GI_L2_WEIGHT;
	const float3 irradiance = EvalGiSH(shSum, n);
#else
	const float3 irradiance = max(shSum[0] * 0.282095f, 0.0f);
#endif
	return irradiance * gDxrGiSpacing.w * fade;
}

// Everything decoded from one triangle hit: material-sampled albedo/ao/
// roughness/metalness, the shading normal (normal-mapped when available),
// and the geometric normal (for shadow-ray offsetting, since a grazing ray
// mustn't self-intersect the origin triangle).
struct HitSurface
{
	uint instanceId;
	uint primitiveIndex;   // originating triangle, for shadow-ray self rejection
	float3 worldPosition;
	float3 rayOrigin;
	// Where shadow rays start: worldPosition lifted onto the smooth surface the
	// vertex normals describe (see DecodeHit). Not the same as rayOrigin, which is
	// only an error-bounded push off the flat triangle.
	float3 shadowPosition;
	float3 albedo;
	float  ao, roughness, metalness;
	float textureMip;
	float roughnessAdjustment;
	// Decoded by MaterialEmissive, exactly as GBufferPS does, so every ray
	// consumer has one authoritative material interpretation.
	float3 emissive;
	float3 worldNormal;      // normal-mapped when the material has one
	float3 geoWorldNormal;   // flat, un-perturbed triangle normal
};

// Bound reconstruction, transform and traversal error before spawning rays.
// Based on NVIDIA's DXR self-intersection error analysis (RTX triangle bound).
// Keep the base vertex and world translation additions last and precise.
void ReconstructRaySurface(float3 p0, float3 p1, float3 p2, float2 bary,
	float3x4 o2w, float3x4 w2o, float3 incoming,
	out float3 position, out float3 normal, out float3 origin)
{
	precise float3 e1 = p1 - p0;
	precise float3 e2 = p2 - p0;
	precise float3 localPosition = p0 + mad(bary.x, e1, bary.y * e2);
	precise float3 world;
	world.x = o2w._m03 + mad(o2w._m00, localPosition.x, mad(o2w._m01, localPosition.y, o2w._m02 * localPosition.z));
	world.y = o2w._m13 + mad(o2w._m10, localPosition.x, mad(o2w._m11, localPosition.y, o2w._m12 * localPosition.z));
	world.z = o2w._m23 + mad(o2w._m20, localPosition.x, mad(o2w._m21, localPosition.y, o2w._m22 * localPosition.z));
	const float3 localNormal = cross(e1, e2);
	const float3 transformedNormal = mul(transpose((float3x3)w2o), localNormal);
	const float invLength = rsqrt(max(dot(transformedNormal, transformedNormal), 1e-30f));
	normal = transformedNormal * invLength;
	if (dot(normal, incoming) > 0.0f) normal = -normal;
	const float c0 = 5.9604644775390625e-8f;
	const float c1 = 1.78813976958736e-7f;
	const float c2 = 1.192093179724907e-7f;
	const float3 extent3 = abs(e1) + abs(e2) + abs(abs(e1) - abs(e2));
	const float extent = max(extent3.x, max(extent3.y, extent3.z));
	float3 objectError = c0 * abs(p0) + c1 * extent;
	const float3 worldError = c1 * mul(abs((float3x3)o2w), abs(localPosition)) +
		c2 * abs(float3(o2w._m03, o2w._m13, o2w._m23));
	objectError += c2 * mul(abs(w2o), float4(abs(world), 1.0f));
	const float offset = dot(worldError, abs(normal)) + invLength * dot(objectError, abs(localNormal));
	// Match TraceShadowRay's centimetre-scale minimum.  Leaving this at a
	// floating-point-only epsilon makes the offset ineffective for large,
	// imported meshes and produces discontinuous per-triangle visibility.
	precise float3 shifted = world + normal * max(offset, 0.05f);
	position = world;
	origin = shifted;
}

// Convert the ray's world-space pixel footprint to texels using triangle UV
// gradients. Each map uses its own dimensions; degenerate UVs stay at mip 0.
float RayTextureMip(uint srv, float2 uvDx, float2 uvDy)
{
	uint width, height, levels;
	gRaySceneTex[NonUniformResourceIndex(srv)].GetDimensions(0u, width, height, levels);
	const float2 dx = uvDx * float2(width, height), dy = uvDy * float2(width, height);
	const float a = dot(dx, dx), b = dot(dx, dy), c = dot(dy, dy);
	const float root = sqrt(max((a - c) * (a - c) + 4.0f * b * b, 0.0f));
	const float major = sqrt(max(0.5f * (a + c + root), 0.0f));
	const float minor = sqrt(max(0.5f * (a + c - root), 0.0f));
	// Approximate the anisotropic sampler's effective mip for the heatmap.
	const float rho = max(minor, major / 16.0f);
	return clamp(log2(max(rho, 1.0f)), 0.0f, float(max(levels, 1u) - 1u));
}

float4 SampleRayTexture(uint srv, float2 uv, float2 uvDx, float2 uvDy)
{
	return gRaySceneTex[NonUniformResourceIndex(srv)].SampleGrad(gMaterialSampler, uv, uvDx, uvDy);
}

float FilterSpecularRoughness(float roughness, float variance, float strength)
{
	if (strength <= 0.0f || variance <= 0.0f) return roughness;
	strength *= saturate((0.7f-roughness)/0.3f);
	if (strength <= 0.0f) return roughness;
	// Broaden GGX alpha squared, not perceptual roughness directly.
	const float kernel = min(2.0f * max(variance, 0.0f) * max(strength, 0.0f), 0.1f);
	return sqrt(sqrt(saturate(pow(saturate(roughness), 4.0f) + kernel)));
}

float3 DecodeNormalXY(float2 encoded)
{
	const float2 xy = encoded * 2.0f - 1.0f;
	return normalize(float3(xy, sqrt(saturate(1.0f-dot(xy,xy)))));
}

HitSurface DecodeHit(RayQuery<RAY_FLAG_CULL_BACK_FACING_TRIANGLES> q, float coneWidth = 0.0f, float coneSpread = 0.0f, float specularAaStrength = 0.0f)
{
	const uint instanceId = q.CommittedInstanceID();
	const RayGeometryLookup g = gRayGeometry[instanceId];

	const uint indexSlot = NonUniformResourceIndex(g.indexSrv);
	const uint vertexSlot = NonUniformResourceIndex(g.vertexSrv);

	// 3 sequential uint32 indices, indexStride is always 4 bytes (Model::MeshData
	// stores indices as unsigned int -- see RayGeometryData's default).
	const uint primByteOffset = q.CommittedPrimitiveIndex() * 3u * 4u;
	const uint3 tri = gRawGeometry[indexSlot].Load3(primByteOffset);

	float3 n[3], t[3], b[3], p[3];
	float2 uv0[3];
	[unroll]
	for (uint i = 0; i < 3; ++i)
	{
		const uint vBase = tri[i] * g.vertexStride;
		p[i] = asfloat(gRawGeometry[vertexSlot].Load3(vBase + g.positionOffset));
		uv0[i] = asfloat(gRawGeometry[vertexSlot].Load2(vBase + g.uv0Offset));
		if (g.vertexFormat == 1u)
		{
			// MeshVertex: four snorm16 (octahedral normal, tangent) and a float
			// bitangent sign. See Tga::MeshVertex.
			const uint packed = gRawGeometry[vertexSlot].Load(vBase + g.normalOffset);
			const uint packed2 = gRawGeometry[vertexSlot].Load(vBase + g.normalOffset + 4u);
			const float2 octN = float2(SnormFromU16(packed & 0xFFFFu), SnormFromU16(packed >> 16));
			const float2 octT = float2(SnormFromU16(packed2 & 0xFFFFu), SnormFromU16(packed2 >> 16));
			n[i] = MeshOctDecode(octN);
			t[i] = MeshOctDecode(octT);
			const float sign = asfloat(gRawGeometry[vertexSlot].Load(vBase + g.binormalOffset)) < 0.0f ? -1.0f : 1.0f;
			b[i] = cross(n[i], t[i]) * sign;
		}
		else
		{
			n[i] = asfloat(gRawGeometry[vertexSlot].Load3(vBase + g.normalOffset));
			t[i] = asfloat(gRawGeometry[vertexSlot].Load3(vBase + g.tangentOffset));
			b[i] = asfloat(gRawGeometry[vertexSlot].Load3(vBase + g.binormalOffset));
		}
	}

	// DXR barycentrics: (u,v) are the weights of vertex 1 and 2; vertex 0's
	// weight is the remainder.
	const float2 bary = q.CommittedTriangleBarycentrics();
	const float w0 = 1.f - bary.x - bary.y;
	const float3 localNormal = n[0] * w0 + n[1] * bary.x + n[2] * bary.y;
	const float2 hitUv = uv0[0] * w0 + uv0[1] * bary.x + uv0[2] * bary.y;
	// Authored per-vertex tangent/binormal (Vertex::tangent/binormal), smoothly
	// interpolated across the triangle exactly like the raster path.
	const float3 localTangent = t[0] * w0 + t[1] * bary.x + t[2] * bary.y;
	const float3 localBinormal = b[0] * w0 + b[1] * bary.x + b[2] * bary.y;

	const float3x4 o2w = q.CommittedObjectToWorld3x4();
	// Normals use inverse transpose; tangent vectors use object-to-world.
	const float3x3 normalToWorld = transpose((float3x3)q.CommittedWorldToObject3x4());

	const RayMaterialRecord mat = gMaterials[g.materialIndex];
	const float3 edge1 = mul((float3x3)o2w, p[1] - p[0]);
	const float3 edge2 = mul((float3x3)o2w, p[2] - p[0]);
	const float3 faceCross = cross(edge1, edge2);
	const float area = length(faceCross);
	// The offset normal must be flat, never the interpolated shading normal.
	// Face the incident ray so mirrored instances also spawn on the hit side.
	float3 geoWorldNormal = area > 1e-8f ? faceCross / area : -normalize(q.WorldRayDirection());
	if (dot(geoWorldNormal, q.WorldRayDirection()) > 0.0f) geoWorldNormal = -geoWorldNormal;

	// Latent asymmetry, deliberately left alone: geoWorldNormal above and
	// ReconstructRaySurface below both re-orient into the ray-facing
	// hemisphere, while this shading normal -- the one ShadeDirect actually
	// lights with -- does not. On content whose authored vertex normals
	// genuinely disagree with their triangle winding (or on a mirrored,
	// negative-determinant instance) that would light the wrong side.
	// Measured on Sponza 2026-09-16: flipping it when it disagrees with
	// geoWorldNormal produced a byte-identical frame, i.e. it never triggers
	// on this content, so the guard was removed again rather than left in as
	// dead code in the hottest function in the renderer. Reinstate it as
	//   if (dot(shadingWorldNormal, geoWorldNormal) < 0) shadingWorldNormal = -shadingWorldNormal;
	// if content ever does show it -- compare against geoWorldNormal, not the
	// ray direction, or silhouettes of correctly authored closed geometry will
	// flip and crack.
	const float3 shadingWorldNormal = normalize(mul(normalToWorld, localNormal));

	float3 worldTangent = normalize(mul((float3x3)o2w, localTangent));
	worldTangent = normalize(worldTangent - shadingWorldNormal * dot(worldTangent, shadingWorldNormal));
	const float3 worldBitangent = normalize(mul((float3x3)o2w, localBinormal));
	float3 gradU = 0.0f, gradV = 0.0f;
	float2 uvDx = 0.0f, uvDy = 0.0f;
	float geometricVariance = 0.0f;
	if (area > 1e-8f && (coneWidth > 0.0f || coneSpread > 0.0f)) {
		const float3 faceNormal = faceCross / area;
		const float2 uvEdge1 = uv0[1] - uv0[0], uvEdge2 = uv0[2] - uv0[0];
		gradU = (uvEdge1.x * cross(edge2, faceNormal) + uvEdge2.x * cross(faceNormal, edge1)) / area;
		gradV = (uvEdge1.y * cross(edge2, faceNormal) + uvEdge2.y * cross(faceNormal, edge1)) / area;
		const float3 rayDir = q.WorldRayDirection();
		const float3 axisX = normalize(abs(rayDir.z) < 0.99f ? cross(rayDir, float3(0,0,1)) : cross(rayDir, float3(0,1,0)));
		const float3 axisY = cross(rayDir, axisX);
		const float nd = dot(faceNormal, rayDir);
		const float safeNd = (nd < 0.0f ? -1.0f : 1.0f) * max(abs(nd), 0.1f);
		const float diameter = max(0.0f, coneWidth + coneSpread * q.CommittedRayT());
		const float3 worldDx = (axisX - rayDir * dot(faceNormal, axisX) / safeNd) * diameter;
		const float3 worldDy = (axisY - rayDir * dot(faceNormal, axisY) / safeNd) * diameter;
		uvDx = float2(dot(gradU, worldDx), dot(gradV, worldDx));
		uvDy = float2(dot(gradU, worldDy), dot(gradV, worldDy));
		const float e11 = dot(edge1,edge1), e22 = dot(edge2,edge2), e12 = dot(edge1,edge2);
		const float determinant = e11*e22-e12*e12;
		if (specularAaStrength > 0.0f && determinant > 1e-8f) {
			const float2 dbx = float2(e22*dot(worldDx,edge1)-e12*dot(worldDx,edge2), e11*dot(worldDx,edge2)-e12*dot(worldDx,edge1))/determinant;
			const float2 dby = float2(e22*dot(worldDy,edge1)-e12*dot(worldDy,edge2), e11*dot(worldDy,edge2)-e12*dot(worldDy,edge1))/determinant;
			const float3 nx = mul(normalToWorld,(n[1]-n[0])*dbx.x+(n[2]-n[0])*dbx.y);
			const float3 ny = mul(normalToWorld,(n[1]-n[0])*dby.x+(n[2]-n[0])*dby.y);
			const float invNormalLength = rcp(max(length(mul(normalToWorld,localNormal)),1e-5f));
			const float3 dxNormal = (nx-shadingWorldNormal*dot(nx,shadingWorldNormal))*invNormalLength;
			const float3 dyNormal = (ny-shadingWorldNormal*dot(ny,shadingWorldNormal))*invNormalLength;
			geometricVariance = 0.25f*(dot(dxNormal,dxNormal)+dot(dyNormal,dyNormal));
		}
	}

	HitSurface s;
	s.instanceId = instanceId;
	s.primitiveIndex = q.CommittedPrimitiveIndex();
	ReconstructRaySurface(p[0], p[1], p[2], bary, o2w, q.CommittedWorldToObject3x4(),
		q.WorldRayDirection(), s.worldPosition, s.geoWorldNormal, s.rayOrigin);

	// Shadow-terminator offset (Hanika, "Hacking the Shadow Terminator",
	// Ray Tracing Gems II). Low-poly meshes with smooth vertex normals are
	// shaded as if curved, but shadow rays leave the flat facet, which sits
	// *below* that implied curved surface wherever the mesh bends away. The
	// neighbouring facets then block the light, giving jagged, triangle-shaped
	// black regions on surfaces the shading says are lit -- exactly what
	// Sponza's swags showed. Push the point out of each vertex's tangent plane
	// it lies beneath, weighted by barycentrics, so shadow rays start from the
	// smooth surface instead. Vertex normals are oriented to the side the ray
	// arrived from, so back-side hits on single-sided cloth lift the right way.
	{
		const float3 wp0 = mul(o2w, float4(p[0], 1.0f));
		const float3 wp1 = mul(o2w, float4(p[1], 1.0f));
		const float3 wp2 = mul(o2w, float4(p[2], 1.0f));
		float3 wn0 = normalize(mul(normalToWorld, n[0]));
		float3 wn1 = normalize(mul(normalToWorld, n[1]));
		float3 wn2 = normalize(mul(normalToWorld, n[2]));
		if (dot(wn0, s.geoWorldNormal) < 0.0f) wn0 = -wn0;
		if (dot(wn1, s.geoWorldNormal) < 0.0f) wn1 = -wn1;
		if (dot(wn2, s.geoWorldNormal) < 0.0f) wn2 = -wn2;
		const float3 P = s.worldPosition;
		const float3 lift =
			-w0     * min(0.0f, dot(P - wp0, wn0)) * wn0
			-bary.x * min(0.0f, dot(P - wp1, wn1)) * wn1
			-bary.y * min(0.0f, dot(P - wp2, wn2)) * wn2;
#if DXR_SHADOW_TERMINATOR_OFFSET
		s.shadowPosition = all(isfinite(lift)) ? P + lift : P;
#else
		s.shadowPosition = P;
#endif
	}
	s.worldNormal = shadingWorldNormal;
	s.textureMip = 0.0f;
	s.roughnessAdjustment = 0.0f;
	float normalVariance = geometricVariance;
	// Matches TextureCooker's own "no source map" constant for the _M
	// channel (CookOne's rgC = 128 -> 0.5 roughness) instead of disagreeing
	// with it. A material with an albedo texture but no cooked/bound ORM map
	// (mat.ormSrv == 0 below) used to fall back to fully-rough here, which
	// renders as a flat, saturated (ao=1,rough=1,metal=0) yellow wash in the
	// ORM debug view and reads as noticeably flatter/less physically
	// plausible than intended in the lit view too.
	s.ao = 1.f; s.roughness = 0.5f; s.metalness = 0.f;
	s.emissive = 0.0f;
	const MaterialParams mp = mat.params;
	if ((mp.flags & MATERIAL_FLAG_USE_TEXTURES) == 0u)
	{
		// Constants-only material (material preview, procedural surfaces).
		s.albedo = mp.baseColorFactor;
		s.roughness = saturate(mp.roughnessFactor);
		s.metalness = saturate(mp.metalnessFactor);
		s.ao = 1.0f;
		s.emissive = MaterialEmissive(mp, s.albedo, 0.0f);
	}
	else if (mat.albedoSrv != 0)
	{
		s.textureMip = RayTextureMip(mat.albedoSrv, uvDx, uvDy);
		s.albedo = SampleRayTexture(mat.albedoSrv, hitUv, uvDx, uvDy).rgb * mp.baseColorFactor;
		if (mat.ormSrv != 0)
		{
			const float3 orm = SampleRayTexture(mat.ormSrv, hitUv, uvDx, uvDy).rgb;
			s.ao = lerp(1.0f, orm.r, mp.aoStrength);
			s.roughness = saturate(orm.g * mp.roughnessFactor);
			s.metalness = saturate(orm.b * mp.metalnessFactor);
		}
		if (mat.normalSrv != 0)
		{
			// Same decode as GBufferPS (DirectX green unless the material says
			// OpenGL); the engine's TBN negates the authored binormal.
			float3 nT = DecodeMaterialNormal(SampleRayTexture(mat.normalSrv, hitUv, uvDx, uvDy).xy, mp);
			const float3x3 tbn = float3x3(worldTangent, -worldBitangent, shadingWorldNormal);
			s.worldNormal = normalize(mul(nT, tbn));
			if (specularAaStrength > 0.0f && s.roughness < 0.7f && (dot(uvDx,uvDx)+dot(uvDy,uvDy)) > 0.0f) {
				// Sample a bounded footprint; never use derivatives across unrelated hits.
				const float2 offsets[4] = {uvDx*0.25f,-uvDx*0.25f,uvDy*0.25f,-uvDy*0.25f};
				[unroll] for (uint sampleIndex=0; sampleIndex<4; ++sampleIndex) {
					const float3 neighbor = DecodeMaterialNormal(SampleRayTexture(mat.normalSrv,hitUv+offsets[sampleIndex],uvDx,uvDy).xy, mp);
					const float3 delta = neighbor-nT;
					normalVariance += dot(delta,delta)*0.25f;
				}
			}
		}
		if (mat.emissiveSrv != 0)
			s.emissive = MaterialEmissive(mp, s.albedo, SampleRayTexture(mat.emissiveSrv, hitUv, uvDx, uvDy));
	}
	else
	{
		// Fallback for materials with no albedo texture registered: a hue
		// keyed off materialIndex, so untextured materials still read as
		// visually distinct instead of collapsing to one color.
		s.albedo = HsvToRgb(float3(frac((float)g.materialIndex * 0.6180339887f), 0.6f, 0.8f));
	}
	const float authoredRoughness = s.roughness;
	s.roughness = FilterSpecularRoughness(s.roughness,normalVariance,specularAaStrength);
	s.roughnessAdjustment = s.roughness-authoredRoughness;
	return s;
}

// Sun + all local point/spot lights, each with its own shadow ray, from one
// decoded hit. `shadowOrigin` should already be offset off the geometric
// normal by the caller (a fixed world-unit bias is usually enough; the
// caller knows its own scene scale best). `sunRadiance` and
// `ambientIntensity` come from DeferredRenderer::Tunables (dxrSunTint *
// dxrSunIntensity, and dxrAmbientIntensity) -- exposed in the DXR ImGui
// panel rather than baked in here, so they're one place to tune instead of
// a shader edit + recompile.
// Cheap per-pixel hash. Lives here rather than in the lighting shader because
// the samplers below need it too.
float Hash01(uint2 p, uint salt)
{
	uint h = p.x * 1664525u + p.y * 1013904223u + salt * 747796405u + 1013904223u;
	h ^= h >> 16; h *= 2246822519u; h ^= h >> 13;
	return (float)(h & 0x00ffffffu) / 16777216.0f;
}

// Resampled importance sampling over the emissive light list.
//
// Evaluating every emissive triangle per pixel is hopeless -- Bistro has tens of
// thousands -- and picking one uniformly is nearly as bad, because almost all of
// them are irrelevant to any given shading point. RIS draws a few cheap
// candidates, weights them by how much they would actually contribute if
// unshadowed, keeps ONE in a reservoir, and pays for a single shadow ray. The
// reservoir's weight then makes that one sample an unbiased estimate of the
// whole list.
//
// This is the sampling half of ReSTIR. Temporal and spatial reuse -- the "R" --
// build on exactly this reservoir, so they can be added without changing the
// shading below.
// Diffuse and specular are returned separately because they join different NRD
// signals -- the diffuse half the indirect one, the specular half the reflection
// one, alongside the hit distance that signal needs to reproject. Added straight
// to the output instead, either half is the only un-denoised term in the frame
// and dominates its noise.
void SampleEmissiveDirect(HitSurface hs, float3 viewDir, uint2 pixel, uint frameIndex, uint candidates,
	uint2 screenSize, int2 previousPixel, bool historyValid,
	out float3 diffuseOut, out float3 specularOut, out float specularHitDistanceOut)
{
	diffuseOut = 0.0f;
	specularOut = 0.0f;
	// 0 = "nothing lit this pixel", which the caller reads as "no opinion on the
	// hit distance" rather than as a light sitting on the surface.
	specularHitDistanceOut = 0.0f;
	const uint pixelIndex = pixel.y * screenSize.x + pixel.x;
	const uint lightCount = gEmissiveLightCount[0];
	if (lightCount == 0u || candidates == 0u)
	{
		LightReservoir empty = (LightReservoir)0;
		gReservoirs[pixelIndex] = empty;
		return;
	}
	const uint usable = min(lightCount, 65536u);
	const float3 kLum = float3(0.2126f, 0.7152f, 0.0722f);

	// Target function at THIS shading point: what the sample would contribute if
	// nothing occluded it. Reused for the temporal sample too, which is the
	// whole point -- a neighbour's sample is only worth keeping in proportion to
	// what it is worth HERE.
	#define RESTIR_TARGET(lightPoint, lightNormal, radiance, outGeom) 		{ 			const float3 toL = (lightPoint) - hs.worldPosition; 			const float d2 = max(dot(toL, toL), 1e-4f); 			const float3 ldir = toL * rsqrt(d2); 			const float ndl = dot(hs.worldNormal, ldir); 			const float ldn = abs(dot((lightNormal), -ldir)); 			outGeom = (ndl > 0.0f && ldn > 0.0f) ? (ndl * ldn) / max(d2, 1e-3f) : 0.0f; 		}

	// ---- this frame's candidates -----------------------------------------
	float weightSum = 0.0f;
	float chosenTarget = 0.0f;
	float3 chosenRadiance = 0.0f, chosenPoint = 0.0f, chosenNormal = 0.0f;

	for (uint c = 0; c < candidates; ++c)
	{
		const uint seed = frameIndex * 9781u + c * 6271u;
		const float r0 = Hash01(pixel + uint2(c * 31u, c * 17u), seed);
		const float r1 = Hash01(pixel + uint2(c * 13u + 7u, c * 29u + 3u), seed + 1u);
		const float r2 = Hash01(pixel + uint2(c * 53u + 11u, c * 41u + 5u), seed + 2u);

		const EmissiveLight light = gEmissiveLights[min((uint)(r0 * usable), usable - 1u)];
		float su = r1, sv = r2;
		if (su + sv > 1.0f) { su = 1.0f - su; sv = 1.0f - sv; }
		const float3 lightPoint = light.p0 + light.e0 * su + light.e1 * sv;
		const float3 lightNormal = normalize(cross(light.e0, light.e1));

		// distSq is floored by the triangle's area: uniform area sampling is only
		// sane while the receiver is further away than the triangle is wide, and
		// closer than that 1/distSq diverges into a firefly.
		const float3 toLight = lightPoint - hs.worldPosition;
		const float distSq = max(dot(toLight, toLight), 1e-4f);
		const float3 l = toLight * rsqrt(distSq);
		const float nDotL = dot(hs.worldNormal, l);
		const float lDotN = abs(dot(lightNormal, -l));
		if (nDotL <= 0.0f || lDotN <= 0.0f) continue;

		const float geometry = (nDotL * lDotN) / max(distSq, light.area);
		const float target = dot(light.radiance, kLum) * geometry;
		if (target <= 0.0f) continue;

		// weight = target / source pdf, and the source pdf for "uniform over the
		// list, uniform over the triangle" is 1 / (lightCount * area).
		//
		// Power-weighted selection was tried here and measured WORSE (frame-to-
		// frame flicker 6909 -> 8200 changed pixels): it ignores distance, so a
		// nearby dim lamp becomes rare and arrives with a huge weight, which is
		// a new firefly rather than a fixed one. A light hierarchy that knows
		// about distance would be the real answer; a flat power CDF is not.
		const float weight = target * (float)usable * light.area;
		weightSum += weight;
		if (Hash01(pixel + uint2(c * 71u, c * 97u), seed + 3u) * weightSum <= weight)
		{
			chosenTarget = target;
			chosenRadiance = light.radiance;
			chosenPoint = lightPoint;
			chosenNormal = lightNormal;
		}
	}

	uint sampleCount = candidates;

	// ---- temporal reuse ---------------------------------------------------
	// Last frame's reservoir for this surface is a free extra candidate, and it
	// carries everything ITS frame had already accumulated. Re-weight it by what
	// its sample is worth at the current shading point, so a sample that no
	// longer helps is dropped rather than smeared forward.
	if (historyValid && previousPixel.x >= 0 && previousPixel.y >= 0
		&& previousPixel.x < (int)screenSize.x && previousPixel.y < (int)screenSize.y)
	{
		const LightReservoir prev = gPrevReservoirs[previousPixel.y * screenSize.x + previousPixel.x];
		// Cap the history's influence. Without this a reservoir keeps compounding
		// its own confidence and stops responding to the scene, and any error it
		// picked up at a disocclusion never washes out.
		const uint prevM = ReservoirSurfaceMatches(prev, hs.worldPosition, hs.worldNormal) ? min(prev.M, candidates * 8u) : 0u;
		if (prevM > 0u && dot(prev.radiance, kLum) > 0.0f && prev.W > 0.0f)
		{
			float geom = 0.0f;
			RESTIR_TARGET(prev.lightPoint, prev.lightNormal, prev.radiance, geom);
			const float prevTarget = dot(prev.radiance, kLum) * geom;
			const float weight = prevTarget * prev.W * (float)prevM;
			if (weight > 0.0f)
			{
				weightSum += weight;
				sampleCount += prevM;
				if (Hash01(pixel + uint2(13u, 7u), frameIndex * 5779u) * weightSum <= weight)
				{
					chosenTarget = prevTarget;
					chosenRadiance = prev.radiance;
					chosenPoint = prev.lightPoint;
					chosenNormal = prev.lightNormal;
				}
			}
		}
	}

	// ---- spatial reuse ----------------------------------------------------
	// Neighbours shading the same surface looked for the same lights and mostly
	// found different ones. Pooling their reservoirs is what stops the outcome
	// depending on whether THIS pixel happened to draw the lamp -- which is the
	// variance that reads as sparks. It costs no extra rays: the neighbours'
	// samples are re-weighted for this shading point, exactly like the temporal
	// one, and only the survivor is ever traced.
	//
	// Neighbours come from the PREVIOUS frame's buffer: the current one is being
	// written by other threads in this same dispatch, so reading it would be a
	// race.
	if (historyValid)
	{
		const float radius = 16.0f;
		for (uint sp = 0; sp < 3u; ++sp)
		{
			const uint seed = frameIndex * 3331u + sp * 7919u;
			const float a = Hash01(pixel + uint2(sp * 5u, sp * 11u), seed) * 6.28318530718f;
			const float r = sqrt(Hash01(pixel + uint2(sp * 23u, sp * 3u), seed + 1u)) * radius;
			const int2 tap = previousPixel + int2(round(cos(a) * r), round(sin(a) * r));
			if (tap.x < 0 || tap.y < 0 || tap.x >= (int)screenSize.x || tap.y >= (int)screenSize.y) continue;

			const LightReservoir n = gPrevReservoirs[tap.y * screenSize.x + tap.x];
			const uint nM = min(n.M, candidates * 8u);
			if (nM == 0u || n.W <= 0.0f || dot(n.radiance, kLum) <= 0.0f) continue;
			if (!ReservoirSurfaceMatches(n, hs.worldPosition, hs.worldNormal)) continue;

			float geom = 0.0f;
			RESTIR_TARGET(n.lightPoint, n.lightNormal, n.radiance, geom);
			const float nTarget = dot(n.radiance, kLum) * geom;
			const float weight = nTarget * n.W * (float)nM;
			if (weight <= 0.0f) continue;

			weightSum += weight;
			sampleCount += nM;
			if (Hash01(pixel + uint2(sp * 17u + 3u, sp * 29u + 7u), seed + 2u) * weightSum <= weight)
			{
				chosenTarget = nTarget;
				chosenRadiance = n.radiance;
				chosenPoint = n.lightPoint;
				chosenNormal = n.lightNormal;
			}
		}
	}

	LightReservoir out_ = (LightReservoir)0;
	if (chosenTarget <= 0.0f || weightSum <= 0.0f || sampleCount == 0u)
	{
		gReservoirs[pixelIndex] = out_;
		return;
	}

	// W makes the single surviving sample an unbiased estimate of the whole list.
	const float W = weightSum / ((float)sampleCount * chosenTarget);

	const float3 toChosen = chosenPoint - hs.worldPosition;
	const float chosenDist = length(toChosen);
	const float3 l = toChosen / max(chosenDist, 1e-4f);
	const float visibility = TraceShadowRay(hs.shadowPosition, l, chosenDist * 0.999f, hs.instanceId, hs.primitiveIndex);

	// Store the reservoir for next frame. An occluded sample is stored with no
	// weight rather than dropped, so the pixel does not immediately re-propose
	// the same blocked light every frame.
	out_.lightPoint = chosenPoint;
	out_.W = visibility > 0.0f ? W : 0.0f;
	out_.radiance = chosenRadiance;
	out_.M = min(sampleCount, candidates * 8u);
	out_.lightNormal = chosenNormal;
	out_.packedSurfaceNormal = PackReservoirNormal(hs.worldNormal);
	out_.surfacePos = hs.worldPosition;
	gReservoirs[pixelIndex] = out_;

	if (visibility <= 0.0f) return;

	float3 kd;
	const float3 specularBrdf = CookTorrance(hs.worldNormal, viewDir, l, hs.albedo, hs.roughness, hs.metalness, kd);
	const float3 diffuseBrdf = kd * hs.albedo / 3.14159265f;

	// estimate = f(y) * W, and f/p^ leaves only the BRDF and the emitter colour:
	// all the geometry is carried by W.
	const float3 common = (chosenRadiance / max(dot(chosenRadiance, kLum), 1e-6f)) * (chosenTarget * W);
	specularHitDistanceOut = chosenDist;
	diffuseOut = diffuseBrdf * common;
	specularOut = specularBrdf * common;

	const float diffuseLum = dot(diffuseOut, kLum);
	if (diffuseLum > 8.0f) diffuseOut *= 8.0f / diffuseLum;
	const float specularLum = dot(specularOut, kLum);
	if (specularLum > 8.0f) specularOut *= 8.0f / specularLum;
	#undef RESTIR_TARGET
}

float3 ShadeDirect(HitSurface hs, float3 shadowOrigin, float3 viewDir, float3 sunDirToLight, uint lightCount,
                    float3 sunRadiance, float ambientIntensity, uint sunSamples = 4u, float sunRotation = 0.0f)
{
	float3 sunLit = 0.f;
	const float sunNdotl = saturate(dot(hs.worldNormal, sunDirToLight));
	if (sunNdotl > 0.f)
	{
		const float sunShadow = TraceSunVisibility(hs.shadowPosition, hs.geoWorldNormal, sunDirToLight, hs.instanceId, hs.primitiveIndex, sunSamples, sunRotation);
		float3 kd;
		const float3 specular = CookTorrance(hs.worldNormal, viewDir, sunDirToLight, hs.albedo, hs.roughness, hs.metalness, kd);
		const float3 diffuse = kd * hs.albedo / 3.14159265f;
		sunLit = (diffuse + specular) * sunRadiance * (sunNdotl * sunShadow);
	}

	float3 localLit = 0.f;
	for (uint li = 0; li < lightCount; ++li)
		localLit += EvaluatePunctualLight(gLights[li], hs.shadowPosition, hs.geoWorldNormal, hs.instanceId, hs.primitiveIndex, hs.worldNormal, viewDir, hs.albedo, hs.roughness, hs.metalness);

	const float3 ambient = (1.0f - hs.metalness) * hs.albedo * hs.ao * ambientIntensity;
	return ambient + sunLit + localLit;
}

// Probe SH represents direction-dependent *diffuse incident radiance*.  Do
// not bake a view-dependent specular lobe or the screen pass's artistic flat
// ambient into it: both become a broad, permanently stored light source once
// the volume has completed a sweep.
float3 ShadeDiffuseDirect(HitSurface hs, float3 shadowOrigin, float3 sunDirToLight, uint lightCount,
	float3 sunRadiance)
{
	float3 result = 0.0f;
	const float sunNdotL = saturate(dot(hs.worldNormal, sunDirToLight));
	if (sunNdotL > 0.0f)
	{
		const float3 kd = (1.0f - hs.metalness) * hs.albedo;
		result += kd * sunRadiance * (sunNdotL * TraceSunVisibility(hs.shadowPosition, hs.geoWorldNormal, sunDirToLight, hs.instanceId, hs.primitiveIndex) / 3.14159265f);
	}

	for (uint li = 0; li < lightCount; ++li)
	{
		const GpuLight light = gLights[li];
		const float3 toLight = light.position - hs.worldPosition;
		const float distanceToLight = length(toLight);
		if (distanceToLight >= light.range || distanceToLight < 1e-4f) continue;
		const float3 l = toLight / distanceToLight;

		float spotFactor = 1.0f;
		if (light.spotCosOuter > 0.0f)
		{
			spotFactor = smoothstep(light.spotCosOuter, light.spotCosInner, dot(light.spotDir, -l));
			if (spotFactor <= 0.0f) continue;
		}

		const float nDotL = saturate(dot(hs.worldNormal, l));
		const float attenuation = PunctualAttenuation(AreaLightDistance(distanceToLight, EffectiveLightRadius(light.radius)), light.range) * spotFactor;
		const float3 lightOrigin = hs.shadowPosition + hs.geoWorldNormal * (dot(hs.geoWorldNormal, l) >= 0.0f ? 0.05f : -0.05f);
		if (nDotL <= 0.0f || attenuation <= 1e-5f ||
			TraceShadowRay(lightOrigin, l, distanceToLight - 0.5f, hs.instanceId, hs.primitiveIndex) <= 0.0f) continue;

		const float3 kd = (1.0f - hs.metalness) * hs.albedo;
		result += kd * light.color * (nDotL * attenuation / 3.14159265f);
	}
	return result;
}

// Roughness-aware Fresnel-Schlick (Sebastien Lagarde's remap, F90 raised
// toward 1 as roughness drops) -- how much of TraceReflection's result to
// blend in on top of the direct BRDF, so mirror-smooth dielectrics/metals
// reflect strongly at grazing angles and rough surfaces barely reflect at all.
float3 FresnelRoughness(float ndotv, float3 f0, float roughness)
{
	const float3 f90 = max(float3(1.f - roughness, 1.f - roughness, 1.f - roughness), f0);
	return f0 + (f90 - f0) * pow(saturate(1.f - ndotv), 5.f);
}

// Sample one GGX reflection direction and return BRDF * NdotL / PDF.  This is
// the unbiased one-sample estimator for a reflected scene ray.  TAA supplies
// the temporal accumulation; callers retain the prefiltered environment at the
// very rough end until a dedicated reflection denoiser exists.
void SampleGgxReflection(float3 viewDir, float3 normal, float roughness, float3 f0,
	float2 xi, out float3 reflectionDir, out float3 brdfOverPdf)
{
	const float noV = max(dot(normal, viewDir), 1e-4f);
	const float alpha = max(roughness * roughness, 0.0004f);
	const float alpha2 = alpha * alpha;
	const float phi = 6.28318530718f * xi.x;
	const float cosTheta = sqrt((1.0f - xi.y) / max(1.0f + (alpha2 - 1.0f) * xi.y, 1e-5f));
	const float sinTheta = sqrt(saturate(1.0f - cosTheta * cosTheta));
	const float3 tangent = normalize(abs(normal.z) < 0.999f ? cross(float3(0,0,1), normal) : cross(float3(0,1,0), normal));
	const float3 bitangent = cross(normal, tangent);
	const float3 h = normalize(tangent * (cos(phi) * sinTheta) + bitangent * (sin(phi) * sinTheta) + normal * cosTheta);
	reflectionDir = normalize(reflect(-viewDir, h));
	const float noL = saturate(dot(normal, reflectionDir));
	const float noH = max(saturate(dot(normal, h)), 1e-4f);
	const float voH = max(saturate(dot(viewDir, h)), 1e-4f);
	if (noL <= 1e-5f) { brdfOverPdf = 0.0f; return; }
	const float k = pow(roughness + 1.0f, 2.0f) * 0.125f;
	const float gV = noV / (noV * (1.0f - k) + k);
	const float gL = noL / (noL * (1.0f - k) + k);
	const float3 f = f0 + (1.0f - f0) * pow(1.0f - voH, 5.0f);
	brdfOverPdf = min(f * (gV * gL * voH / max(noH * noV, 1e-4f)), 32.0f);
}

// Single-bounce reflection ray: the caller supplies a GGX-sampled reflected
// direction, then this traces it and shades whatever it hits with the SAME
// direct-lighting model (one bounce only -- it does NOT itself call
// TraceReflection again, so this can't runaway/recurse). Sky-colored on a
// miss. Call sites should skip this entirely above a roughness cutoff; a
// rough surface's reflection is too blurred for a single unfiltered sample
// to read as anything but noise.
// Distance to the surface the last TraceReflection call hit (65504, NRD's
// FP16 "infinitely far", for a sky miss). A static rather than an out
// parameter so existing call sites and default arguments stay as they are.
static float gLastReflectionHitDistance = 65504.0f;

float3 TraceReflection(float3 origin, float3 dir, float3 sunDirToLight, uint lightCount,
	                        float3 sunRadiance, float ambientIntensity, float environmentMip, float3 environmentTint, bool enableIndirectGi, float coneWidth, float coneSpread, float specularAaStrength = 0.0f)
{
	RayDesc ray;
	ray.Origin = origin;
	ray.Direction = dir;
	ray.TMin = 0.05f;
	ray.TMax = 100000.f;

	RayQuery<RAY_FLAG_CULL_BACK_FACING_TRIANGLES> q;
	q.TraceRayInline(gScene, RAY_FLAG_NONE, 0xFF, ray);
	while (q.Proceed()) {
		if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
			AcceptRayTriangle(q.CandidateInstanceID(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics()))
			q.CommitNonOpaqueTriangleHit();
	}

	if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
	{
		gLastReflectionHitDistance = 65504.0f;
		return SkyRadiance(dir, environmentMip, environmentTint);
	}
	gLastReflectionHitDistance = q.CommittedRayT();

	const HitSurface hs = DecodeHit(q, coneWidth, coneSpread, specularAaStrength);
	const float3 hitPos = hs.worldPosition;
	const float3 shadowOrigin = hs.rayOrigin;
	const float3 viewDir = normalize(-ray.Direction);
	// One sun-shadow ray per reflection hit instead of the four-ray disc used on
	// primary hits. Every reflection sample runs a full direct shade on what it
	// hits, so the disc cost 4 x dxrReflectionSamples shadow rays per reflective
	// pixel -- measured as most of a 56 ms reflection pass on the Bistro scene,
	// where glossy surfaces are common and the sun is overhead. A reflected
	// penumbra is a few texels wide and already blurred by the GGX lobe and the
	// temporal resolve, so the extra three rays bought nothing visible.
	const float3 lit = ShadeDirect(hs, shadowOrigin, viewDir, sunDirToLight, lightCount, sunRadiance, ambientIntensity, DXR_REFLECTION_SUN_SAMPLES) + hs.emissive;
	// Environment at the reflected hit, diffuse AND specular.
	//
	// Every indirect term here is weighted by (1 - metalness), because a metal
	// has no diffuse lobe -- so without the specular half a metal seen in a
	// reflection had only ShadeDirect left, and a *smooth* metal's GGX sun lobe
	// is nearly a delta function: miss the sun and the surface came back pure
	// black. That is what turned curtain rails and rims black in reflections.
	// This is the same split-sum evaluation EvaluateEnvironmentLighting applies
	// to primary hits, with the analytic Karis fit instead of the BRDF LUT,
	// which is not bound for secondary rays.
	float3 environment = 0.0f;
	if (environmentMip >= 0.0f)
	{
		uint faceWidth, faceHeight, mipCount;
		gDxrEnvironment.GetDimensions(0, faceWidth, faceHeight, mipCount);
		const float maxMip = float(max(int(mipCount) - 1, 0));
		const float diffuseMip = max(0.0f, maxMip - 3.0f);   // CubemapPrefilter's irradiance tail
		const float p = saturate(hs.roughness);
		const float specularMip = (p * (1.7f - 0.7f * p)) * diffuseMip;
		const float nDotV = saturate(dot(hs.worldNormal, viewDir));
		const float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), hs.albedo, hs.metalness);
		const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
		const float4 c1 = float4( 1.0f,  0.0425f, 1.04f, -0.04f);
		const float4 r = p * c0 + c1;
		const float a004 = min(r.x * r.x, exp2(-9.28f * nDotV)) * r.x + r.y;
		const float2 ab = float2(-1.04f, 1.04f) * a004 + r.zw;
		const float3 specularWeight = f0 * ab.x + ab.y;
		const float3 diffuseEnv = gDxrEnvironment.SampleLevel(gMaterialSampler, hs.worldNormal, diffuseMip).rgb * environmentTint;
		const float3 specularEnv = gDxrEnvironment.SampleLevel(gMaterialSampler, reflect(-viewDir, hs.worldNormal), specularMip).rgb * environmentTint;
		environment = (1.0f - saturate(specularWeight)) * (1.0f - hs.metalness) * hs.albedo * hs.ao * diffuseEnv
			+ specularEnv * specularWeight * hs.ao;
	}
	float3 result = lit + environment + (enableIndirectGi ? (1.0f - hs.metalness) * hs.albedo * hs.ao * EvaluateDxrGi(hitPos, hs.worldNormal) : 0.0f);
	
	// Clamp fireflies from extremely bright secondary hits (e.g. emissives)
	const float lum = dot(result, float3(0.2126f, 0.7152f, 0.0722f));
	if (lum > 10.0f)
		result *= 10.0f / lum;
		
	return result;
}

#endif
